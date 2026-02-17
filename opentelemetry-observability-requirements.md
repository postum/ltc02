# OpenTelemetry Observability Stack — Requirements

> **Status**: Draft
> **Date**: 2026-02-17
> **Scope**: Полный observability-стек на базе OpenTelemetry для гибридной платформы (15–50 JVM-сервисов)

---

## 1. Контекст и текущее состояние

### 1.1 Архитектура системы

| Параметр | Значение |
|---|---|
| Архитектура | Гибридная (микросервисы + монолитные компоненты) |
| Количество сервисов | 15–50 |
| Языки/рантаймы | Java, Kotlin (JVM-стек) |
| Межсервисные транспорты | REST (HTTP), gRPC, message brokers (Kafka/RabbitMQ/NATS), возможно GraphQL |
| Инфраструктура | Смешанная: Kubernetes + VM/bare metal + облачные managed-сервисы |

### 1.2 Существующий мониторинг

| Компонент | Текущее состояние | Стратегия |
|---|---|---|
| **Splunk Enterprise** | Используется для логов | Сосуществование — Splunk остаётся, OTel параллельно |
| **Prometheus + Grafana** | Используется для метрик | Интеграция — OTel Collector будет экспортировать в существующий Prometheus |
| Distributed tracing | Отсутствует | Новое внедрение через OTel |
| Continuous profiling | Отсутствует | Новое внедрение |

---

## 2. Сигналы телеметрии

### 2.1 Distributed Traces

**Приоритет**: Высокий (основная мотивация внедрения OTel)

- **Требования**:
  - [ ] Автоматическая инструментация JVM-сервисов (OTel Java Agent)
  - [ ] Пропагация контекста через все транспорты: HTTP, gRPC, Kafka/message broker headers
  - [ ] Корреляция trace ↔ logs через `trace_id`/`span_id` в логах (Splunk и новый стек)
  - [ ] Поддержка sampling-стратегий: head-based и tail-based sampling
  - [ ] Визуализация trace waterfall с отображением cross-service dependencies

- **Ключевые вопросы для уточнения**:
  - [ ] Какой формат пропагации контекста использовать? (W3C TraceContext — рекомендуется, B3 — если есть legacy)
  - [ ] Нужен ли tail-based sampling на фазе 1 или достаточно head-based?
  - [ ] Какой целевой sampling rate для production? (рекомендация: 1–10% head-based + 100% для ошибок)

### 2.2 Metrics

**Приоритет**: Высокий (расширение существующего Prometheus)

- **Требования**:
  - [ ] OTel SDK-метрики из приложений → OTel Collector → Prometheus (remote_write или OTLP)
  - [ ] Сохранение существующих Prometheus-метрик и дашбордов Grafana
  - [ ] Runtime-метрики JVM: heap, GC, threads, class loading
  - [ ] Бизнес-метрики через OTel Metrics API (counters, histograms, gauges)
  - [ ] Инфраструктурные метрики: node_exporter (VM), kube-state-metrics (K8s)
  - [ ] RED-метрики (Rate, Errors, Duration) для каждого сервиса — автоматически из span-метрик

- **Ключевые вопросы для уточнения**:
  - [ ] Оставлять ли текущий Prometheus как есть или мигрировать на Mimir/Thanos/VictoriaMetrics для long-term storage?
  - [ ] Нужен ли Prometheus remote_write или переход на OTLP-native приём метрик?
  - [ ] Как совместить scrape-based (Prometheus) и push-based (OTel) модели сбора?

### 2.3 Logs

**Приоритет**: Средний (Splunk остаётся, но нужна корреляция)

- **Требования**:
  - [ ] Инъекция `trace_id` и `span_id` в логи JVM-сервисов (MDC для Log4j2/Logback)
  - [ ] Дуальный экспорт: логи → Splunk (существующий) + OTel Collector (для корреляции)
  - [ ] Структурированное логирование (JSON) для новых сервисов
  - [ ] OTel Collector как агрегатор логов: приём через filelog receiver или OTLP

- **Ключевые вопросы для уточнения**:
  - [ ] Нужен ли самостоятельный бэкенд для логов (Loki, ClickHouse) или достаточно Splunk + trace correlation?
  - [ ] Как организовать dual-write логов без дублирования ресурсов?
  - [ ] Формат логов на VM vs. K8s — унифицировать через OTel Collector?

### 2.4 Continuous Profiling

**Приоритет**: Низкий (фаза 2+)

- **Требования**:
  - [ ] CPU и allocation profiling для JVM-сервисов
  - [ ] Корреляция профилей с traces (span → profile)
  - [ ] Минимальный overhead в production (< 1% CPU)

- **Ключевые вопросы для уточнения**:
  - [ ] Pyroscope (Grafana) vs. async-profiler + custom решение?
  - [ ] Какой формат хранения профилей? (pprof)
  - [ ] На каком этапе внедрять — только после стабилизации traces + metrics?

---

## 3. Архитектура сбора данных (OTel Collector)

### 3.1 Deployment-паттерны

| Среда | Паттерн | Описание |
|---|---|---|
| **Kubernetes** | DaemonSet (agent) + Deployment (gateway) | Agent на каждом узле собирает telemetry, gateway агрегирует и маршрутизирует |
| **VM / bare metal** | Standalone agent | OTel Collector как systemd-сервис на каждой машине |
| **Managed-сервисы** | Gateway-only | Сервисы отправляют OTLP напрямую на gateway |

- **Требования**:
  - [ ] Единая конфигурация Collector для всех сред (с переменными окружения)
  - [ ] Отказоустойчивость gateway-слоя (минимум 2 инстанса, load balancing)
  - [ ] Backpressure и retry при недоступности бэкендов
  - [ ] Resource limits для Collector (CPU, memory) — особенно на DaemonSet

### 3.2 Пайплайн Collector

```
Receivers → Processors → Exporters

Receivers:
  - otlp (gRPC + HTTP) — от SDK приложений
  - prometheus — scrape существующих /metrics endpoints
  - filelog — логи с VM
  - hostmetrics — инфраструктурные метрики

Processors:
  - batch — группировка для эффективной отправки
  - memory_limiter — защита от OOM
  - attributes — обогащение (environment, service, version)
  - filter — фильтрация ненужных данных
  - tail_sampling (на gateway) — sampling по критериям
  - transform — PII masking

Exporters:
  - otlphttp → Tempo/Jaeger (traces)
  - prometheusremotewrite → Prometheus/Mimir (metrics)
  - otlphttp → Loki (logs) или splunk_hec → Splunk (logs)
  - debug (dev-only)
```

### 3.3 Ключевые вопросы для уточнения

- [ ] Какой дистрибутив Collector использовать? (contrib — рекомендуется, custom build для минимизации)
- [ ] Нужен ли OTel Operator для K8s (auto-instrumentation injection)?
- [ ] Как доставлять конфигурацию Collector? (ConfigMap в K8s, Ansible/Puppet для VM)

---

## 4. Бэкенды хранения (Self-Hosted)

### 4.1 Целевой стек

| Сигнал | Бэкенд (рекомендация) | Альтернативы | Визуализация |
|---|---|---|---|
| **Traces** | Grafana Tempo | Jaeger, SigNoz, ClickHouse | Grafana |
| **Metrics** | Prometheus (существующий) → рассмотреть Mimir | VictoriaMetrics, Thanos | Grafana (существующий) |
| **Logs** | Splunk (существующий) + Grafana Loki (новый) | ClickHouse, OpenSearch | Grafana + Splunk UI |
| **Profiles** | Grafana Pyroscope | — | Grafana |

### 4.2 Retention-политика

| Сигнал | Hot (быстрый доступ) | Warm/Cold | Обоснование |
|---|---|---|---|
| **Traces** | 7–14 дней | 30 дней (объектное хранилище) | Трейсы объёмные, основная ценность — в оперативной диагностике |
| **Metrics** | 30 дней (full resolution) | 90+ дней (downsampled) | Метрики нужны для трендов, компактны после downsampling |
| **Logs** | Определяется Splunk-политикой | — | Следуем текущей Splunk retention policy |
| **Profiles** | 7 дней | 14 дней | Профили тяжёлые, нужны только для активных расследований |

### 4.3 Ключевые вопросы для уточнения

- [ ] Есть ли объектное хранилище (S3, MinIO, GCS) для cold storage Tempo/Loki?
- [ ] Ограничения по дисковому пространству и IOPS для self-hosted бэкендов?
- [ ] Нужна ли multi-tenancy (разделение данных по командам/средам)?

---

## 5. Security и Compliance

### 5.1 Базовые требования

- [ ] **TLS**: Шифрование трафика между всеми компонентами OTel (Collector ↔ SDK, Collector ↔ бэкенды)
- [ ] **PII masking**: Удаление/маскирование персональных данных в span attributes и логах
  - Email, телефоны, IP-адреса, токены в URL query params
  - Реализация через `transform` processor в OTel Collector
- [ ] **Authentication**: mTLS или API-ключи для доступа к Collector gateway
- [ ] **RBAC**: Разграничение доступа в Grafana по командам/сервисам
- [ ] **Network policy**: Ограничение сетевого доступа к бэкендам (только от Collector)

### 5.2 Ключевые вопросы для уточнения

- [ ] Есть ли PKI/cert-manager для выпуска TLS-сертификатов?
- [ ] Какие конкретно поля/паттерны нужно маскировать?
- [ ] Требуется ли audit log для доступа к observability-данным?

---

## 6. Alerting и On-Call

### 6.1 Требования

- [ ] Alerting на основе метрик через существующий Prometheus Alertmanager
- [ ] SLO-based alerting: error rate, latency percentiles (p50, p95, p99)
- [ ] Алерты на аномалии в trace data: рост error rate, всплеск latency
- [ ] Интеграция с системой дежурств (PagerDuty, OpsGenie, Slack)
- [ ] Runbook-ссылки в алертах для ускорения incident response

### 6.2 Ключевые вопросы для уточнения

- [ ] Какая система дежурств используется сейчас?
- [ ] Есть ли определённые SLO/SLI для сервисов?
- [ ] Нужна ли автоматическая генерация алертов из OTel span-метрик?

---

## 7. Пользовательский опыт (Developer Experience)

### 7.1 Для разработчиков

- [ ] Auto-instrumentation через OTel Java Agent (zero-code для базовых сигналов)
- [ ] SDK-библиотеки для кастомных спанов и метрик (internal library/starter)
- [ ] Документация: как добавить tracing в новый сервис (< 30 минут на onboarding)
- [ ] Local dev: возможность видеть трейсы локально (Jaeger all-in-one / OTel dev collector)
- [ ] Grafana-дашборды: шаблон "Service Overview" для каждого сервиса (RED-метрики + runtime)

### 7.2 Для SRE/DevOps

- [ ] Service map / dependency graph на основе trace data
- [ ] Cross-service trace exploration с фильтрацией по атрибутам
- [ ] Корреляция: trace → logs → metrics в одном UI (Grafana Explore)
- [ ] Capacity planning: метрики объёма телеметрии, стоимость хранения
- [ ] Runbook: как диагностировать инцидент с помощью observability-стека

### 7.3 Ключевые вопросы для уточнения

- [ ] Нужен ли shared internal library (Java starter) для стандартизации инструментации?
- [ ] Есть ли CI/CD pipeline, в который нужно интегрировать OTel Agent?
- [ ] Требуется ли обучение команд по работе с новым стеком?

---

## 8. Фазы внедрения (предварительно)

> Точный план реализации будет составлен отдельно на основании утверждённых требований.

### Фаза 0 — Фундамент (~ 2–4 недели)
- OTel Collector: deployment в K8s (DaemonSet + Gateway) и на VM
- Базовая конфигурация пайплайна (receivers, processors, exporters)
- Grafana Tempo для traces
- Интеграция с существующим Prometheus

### Фаза 1 — Трассировка (~ 4–6 недель)
- Auto-instrumentation 3–5 пилотных сервисов (Java Agent)
- Context propagation через все транспорты
- Trace ↔ log correlation (trace_id в MDC → Splunk)
- Базовые дашборды и алерты

### Фаза 2 — Масштабирование (~ 4–8 недель)
- Rollout на все сервисы
- Tail-based sampling на gateway
- PII masking pipeline
- SLO-based alerting
- Loki для логов (параллельно со Splunk)

### Фаза 3 — Зрелость (~ ongoing)
- Continuous profiling (Pyroscope)
- Custom instrumentation и бизнес-метрики
- Service map и dependency analysis
- Capacity optimization и cost monitoring
- Обучение и документация

---

## 9. Нефункциональные требования

| Требование | Значение | Примечание |
|---|---|---|
| **Overhead на приложение** | < 3% CPU, < 50 MB RAM | OTel Java Agent overhead |
| **Latency добавленная** | < 1 ms на span export | Async export, batch processing |
| **Доступность Collector** | 99.9% | Gateway — HA с минимум 2 репликами |
| **Потеря данных** | Допустимо < 0.1% при нормальной работе | Retry + persistent queue в Collector |
| **Время от деплоя до видимости** | < 2 минут | Трейсы/метрики доступны в Grafana |
| **Масштабируемость** | До 100 сервисов без изменения архитектуры | Горизонтальное масштабирование gateway |

---

## 10. Открытые вопросы и решения

> Эти вопросы должны быть закрыты до начала реализации.

| # | Вопрос | Варианты | Решение |
|---|---|---|---|
| Q1 | Формат пропагации контекста | W3C TraceContext / B3 / оба | **TBD** |
| Q2 | Long-term metrics storage | Оставить Prometheus / Mimir / VictoriaMetrics | **TBD** |
| Q3 | Самостоятельный лог-бэкенд | Loki / ClickHouse / только Splunk | **TBD** |
| Q4 | OTel Operator в K8s | Да (auto-inject agent) / Нет (ручной деплой) | **TBD** |
| Q5 | Дистрибутив Collector | otelcol-contrib / custom build | **TBD** |
| Q6 | Объектное хранилище для cold data | S3 / MinIO / GCS / нет | **TBD** |
| Q7 | Multi-tenancy | По командам / по средам / не нужна | **TBD** |
| Q8 | CI/CD интеграция Java Agent | Docker layer / init container / sidecar | **TBD** |
| Q9 | Система дежурств для алертов | PagerDuty / OpsGenie / Slack / другое | **TBD** |
| Q10 | Обучение команд | Внутреннее / внешнее / self-service docs | **TBD** |

---

## Приложение A: Глоссарий

| Термин | Описание |
|---|---|
| **OTel** | OpenTelemetry — CNCF-проект, стандарт для сбора телеметрии |
| **Collector** | OTel Collector — агент/gateway для приёма, обработки и экспорта телеметрии |
| **Span** | Единица работы в распределённом трейсе (вызов метода, HTTP-запрос) |
| **RED** | Rate, Errors, Duration — ключевые метрики для микросервисов |
| **MDC** | Mapped Diagnostic Context — механизм Log4j2/Logback для добавления контекста в логи |
| **Tail-based sampling** | Решение о сохранении трейса принимается после завершения всех спанов |
| **Head-based sampling** | Решение о сохранении принимается на входе в систему (в первом сервисе) |
