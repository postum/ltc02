# OpenTelemetry Observability Stack — Requirements

> **Status**: Draft v2
> **Date**: 2026-02-17
> **Scope**: OpenTelemetry-based observability stack for a hybrid platform (OpenShift + Linux VMs, JVM services, Oracle DB)

---

## 1. Context and Current State

### 1.1 Infrastructure

| Component | Description |
|---|---|
| **Linux VMs** | Several dozen Linux virtual machines |
| **OpenShift** | Several dozen namespaces running Java/Kotlin applications |
| **Databases** | Oracle DB |
| **Languages/Runtimes** | Java, Kotlin (JVM stack) |
| **Inter-service transports** | REST (HTTP), gRPC, message brokers, possibly GraphQL |

### 1.2 Existing Prometheus Exporters

Already in use or requiring integration:

| Exporter | Environment | Purpose |
|---|---|---|
| **node_exporter** | Linux VM | OS metrics: CPU, RAM, disk, network, filesystem |
| **ssl_exporter** | Linux VM | TLS certificate expiry monitoring, chain validity |
| **process_exporter** | Linux VM | Per-process metrics: CPU, RSS, open FDs, threads |
| **oracledb_exporter** | Oracle DB | Database metrics: sessions, tablespaces, wait events, query performance |
| **blackbox_exporter** | Network level | Probe monitoring: HTTP, TCP, ICMP, DNS — endpoint availability and latency |

### 1.3 Current Monitoring

| Component | Current State | Strategy |
|---|---|---|
| **Prometheus + Grafana** | Used for metrics (exporters listed above) | Migration to VictoriaMetrics as long-term storage |
| **Splunk Enterprise** | Used for logs | Coexistence, gradual transition to OpenSearch |
| Distributed tracing | Not implemented | New deployment via OTel |
| Continuous profiling | Not implemented | Phase 2+ |

---

## 2. Telemetry Signals

### 2.1 Metrics

**Priority**: High

#### 2.1.1 Infrastructure Metrics (VM)

- [ ] Collect metrics from existing exporters via OTel Collector (`prometheus` receiver)
  - `node_exporter` — scrape from each VM
  - `ssl_exporter` — scrape centrally or from each VM
  - `process_exporter` — scrape from each VM
- [ ] Export to VictoriaMetrics via `prometheusremotewrite` exporter
- [ ] Maintain compatibility with existing Grafana dashboards and recording rules

#### 2.1.2 Database Metrics

- [ ] `oracledb_exporter` → OTel Collector (`prometheus` receiver) → VictoriaMetrics
- [ ] Monitoring: sessions, tablespace usage, wait events, top SQL, connection pool
- [ ] Alerts: tablespace > 80%, blocked sessions, long-running queries

#### 2.1.3 Probe Monitoring

- [ ] `blackbox_exporter` → OTel Collector → VictoriaMetrics
- [ ] HTTP probes for key endpoints (health checks, API endpoints)
- [ ] TCP probes for infrastructure services (Oracle listener, Kafka brokers)
- [ ] SSL certificate expiry alerting (in addition to ssl_exporter)

#### 2.1.4 Application Metrics (OpenShift)

- [ ] JVM runtime metrics via OTel Java Agent: heap, GC, threads, class loading
- [ ] Business metrics via OTel Metrics API (counters, histograms, gauges)
- [ ] RED metrics (Rate, Errors, Duration) — automatically derived from span metrics (spanmetrics connector)
- [ ] OpenShift-specific metrics: kube-state-metrics, kubelet metrics

#### 2.1.5 Key Questions

- [ ] Which recording/alerting rules are already defined in current Prometheus? Do they need migration?
- [ ] Is federation or vmagent needed for collecting metrics from OpenShift built-in monitoring?
- [ ] Service discovery for VM exporters — DNS-based, file-based, or Consul?

### 2.2 Distributed Traces

**Priority**: High

- **Requirements**:
  - [ ] Automatic instrumentation of JVM services in OpenShift (OTel Java Agent)
  - [ ] Context propagation across all transports: HTTP, gRPC, Kafka/message broker headers
  - [ ] Trace ↔ log correlation via `trace_id`/`span_id` (MDC injection in Log4j2/Logback)
  - [ ] Support for sampling strategies: head-based and tail-based sampling
  - [ ] Trace waterfall visualization, service map, dependency graph

- **Key Questions**:
  - [ ] Context propagation format: W3C TraceContext (recommended) or B3 (if legacy exists)?
  - [ ] Tail-based sampling in phase 1 or is head-based sufficient?
  - [ ] Target sampling rate for production (recommendation: 1–10% head-based + 100% for errors)

### 2.3 Logs

**Priority**: Medium

- **Requirements**:
  - [ ] Inject `trace_id`, `span_id` into JVM application logs (MDC)
  - [ ] Collect logs from OpenShift via OTel Collector (filelog receiver / k8s container logs)
  - [ ] Collect logs from VMs via OTel Collector (filelog receiver)
  - [ ] Structured logging (JSON) for new services
  - [ ] Export to OpenSearch for storage and full-text search
  - [ ] Log ↔ trace correlation in Grafana (via OpenSearch data source + trace-to-logs)

- **Key Questions**:
  - [ ] Migration strategy from Splunk to OpenSearch — big bang or gradual per-service rollout?
  - [ ] Is dual-write (Splunk + OpenSearch) needed during transition?
  - [ ] Log format unification: VM (syslog/files) vs. OpenShift (stdout/stderr)

### 2.4 Continuous Profiling

**Priority**: Low (phase 2+, but architecture should be planned now)

- **Requirements**:
  - [ ] CPU and allocation profiling for JVM services
  - [ ] Profile ↔ trace correlation (span → profile)
  - [ ] Minimal production overhead (< 1% CPU)

- **Key Questions**:
  - [ ] Pyroscope (Grafana) vs. async-profiler + custom solution?
  - [ ] When to implement — only after traces + metrics are stable?

---

## 3. Data Collection Architecture (OTel Collector)

### 3.1 Deployment Patterns

| Environment | Pattern | Description |
|---|---|---|
| **OpenShift** | DaemonSet (agent) + Deployment (gateway) | Agent on each node, gateway for aggregation and routing |
| **Linux VM** | Standalone agent (systemd) | OTel Collector as a systemd service on each machine |

### 3.2 Centralized Configuration Management (OpAMP)

**Key Requirement**: All Collector instances are managed centrally via the **OpAMP** (Open Agent Management Protocol) protocol.

- [ ] **OpAMP Server**: central component for configuration management
  - Storage and versioning of Collector configurations
  - Push configuration updates to agents without restart
  - Monitor status of all agents (health, version, effective config)
- [ ] **OpAMP Supervisor** on each Collector:
  - Receive configuration updates from OpAMP Server
  - Apply configuration locally with graceful reload
  - Report status and effective config back to server
- [ ] **Agent grouping**:
  - By environment type: `openshift-agent`, `openshift-gateway`, `vm-agent`
  - By function: `metrics-pipeline`, `traces-pipeline`, `logs-pipeline`
  - By deployment environment: `prod`, `staging`, `dev`
- [ ] **Rollout strategy**: canary configuration updates (single agent → group → all)

#### Key Questions (OpAMP)

- [ ] Which OpAMP server to use? (BindPlane OP — most mature, OpAMP-go reference, custom)
- [ ] Is a UI needed for configuration management or is API + GitOps sufficient?
- [ ] How to ensure fallback when OpAMP Server is unavailable? (local config as fallback)
- [ ] OpAMP integration with existing CI/CD system (configurations as code in git)?

### 3.3 Collector Pipeline

```
Receivers → Processors → Connectors → Exporters

═══════════════════════════════════════════════════════
VM Agent pipeline:
═══════════════════════════════════════════════════════

Receivers:
  - prometheus         ← scrape node_exporter, ssl_exporter,
                         process_exporter, oracledb_exporter
  - filelog            ← application and system logs
  - hostmetrics        ← additional host metrics

Processors:
  - memory_limiter     ← OOM protection
  - batch              ← batching for efficient export
  - attributes         ← enrichment: environment, host, datacenter
  - filter             ← filter out unnecessary metrics
  - transform          ← PII masking in logs

Exporters:
  - prometheusremotewrite → VictoriaMetrics (metrics)
  - otlphttp              → OpenSearch (logs)

═══════════════════════════════════════════════════════
OpenShift Agent pipeline (DaemonSet):
═══════════════════════════════════════════════════════

Receivers:
  - otlp (gRPC + HTTP) ← from Java Agent in pods
  - filelog            ← container stdout/stderr logs
  - k8s_events         ← Kubernetes events

Processors:
  - memory_limiter
  - k8sattributes      ← enrich with pod/namespace/deployment metadata
  - batch
  - transform          ← PII masking

Exporters:
  - otlp → Gateway

═══════════════════════════════════════════════════════
OpenShift Gateway pipeline (Deployment):
═══════════════════════════════════════════════════════

Receivers:
  - otlp (gRPC + HTTP) ← from agents
  - prometheus         ← scrape blackbox_exporter, kube-state-metrics

Connectors:
  - spanmetrics        ← generate RED metrics from spans

Processors:
  - memory_limiter
  - batch
  - tail_sampling      ← sampling by error status, latency, service
  - attributes         ← attribute normalization
  - transform          ← PII masking

Exporters:
  - prometheusremotewrite → VictoriaMetrics (metrics + spanmetrics)
  - otlphttp              → Tempo or Jaeger (traces)
  - otlphttp              → OpenSearch (logs)
```

### 3.4 Collector Requirements

- [ ] Single Collector binary for OpenShift and VM (one distribution, different configs via OpAMP)
- [ ] Gateway high availability: minimum 2 replicas, load balancing
- [ ] Backpressure and retry on backend unavailability
- [ ] Persistent queue for buffering during transient backend failures
- [ ] Resource limits: CPU and memory constraints (especially on DaemonSet)
- [ ] Self-monitoring: Collector's own metrics (queue size, dropped data, export errors) → VictoriaMetrics

### 3.5 Key Questions

- [ ] Distribution: `otelcol-contrib` (batteries included) or custom build (minimal component set)?
- [ ] Is OpenTelemetry Operator needed for OpenShift (auto-injection of Java Agent)?
- [ ] How to handle service discovery for VM exporters (file_sd, DNS-SD, Consul)?

---

## 4. Storage Backends (Self-Hosted, OSS)

### 4.1 Target Stack

| Signal | Backend | Rationale | Visualization |
|---|---|---|---|
| **Metrics** | **VictoriaMetrics** | High performance, PromQL compatibility, long-term storage, compression | Grafana |
| **Traces** | **Tempo** or **Jaeger** | OSS, Grafana integration; Tempo — object storage native, Jaeger — mature | Grafana |
| **Logs** | **OpenSearch** | Full-text search, analytics, Splunk replacement; mature ecosystem | OpenSearch Dashboards + Grafana |
| **Profiles** | **Pyroscope** (phase 2) | Grafana integration, JVM support, trace correlation | Grafana |

### 4.2 VictoriaMetrics — Architecture

- [x] **Deployment mode**: Cluster (vminsert + vmselect + vmstorage)
  - Horizontal scaling: each component scales independently
  - HA: multiple replicas of vminsert and vmselect, replication factor for vmstorage
  - vminsert: stateless, accepts remote_write, distributes data across vmstorage nodes
  - vmselect: stateless, executes PromQL queries across all vmstorage nodes
  - vmstorage: stateful, stores time series data on local disk
- [ ] **vmauth**: routing proxy in front of vminsert/vmselect for load balancing, authentication, and tenant routing
- [ ] Data ingestion: Prometheus remote_write from OTel Collector → vminsert
- [ ] Compatibility: PromQL queries from Grafana → vmselect, existing dashboards work without changes
- [ ] vmalert for rule evaluation (recording rules + alerting rules) → Alertmanager
- [ ] vmagent as optional scrape component (if pull from VMs without OTel Collector is needed)
- [ ] **Replication**: `-replicationFactor=2` on vminsert for data durability (writes to N vmstorage nodes)
- [ ] **Deduplication**: `-dedup.minScrapeInterval` on vmselect to handle HA/replication duplicates

#### Key Questions (VictoriaMetrics)

- [ ] How many vmstorage nodes initially? (recommendation: minimum 3 for replication factor 2)
- [ ] Storage capacity per vmstorage node? (depends on retention and active time series volume)
- [ ] Is vmagent needed separately or does OTel Collector fully replace it for scraping?
- [ ] Deploy on OpenShift (StatefulSet) or on dedicated VMs?

### 4.3 Tempo / Jaeger — Trace Backend Selection

| Criterion | Tempo | Jaeger |
|---|---|---|
| Storage | Object storage (S3/MinIO) — cost-effective for large volumes | Elasticsearch/Cassandra/ClickHouse — more complex but flexible |
| Search | By trace ID; tag-based search via Tempo + search backend | Full tag-based search out of the box |
| Grafana integration | Native (TraceQL) | Via data source plugin |
| Maturity | Relatively young | Mature, CNCF graduated |
| Operational complexity | Low (stateless + object storage) | Medium (depends on storage backend) |

#### Key Questions (Traces)

- [ ] Tempo vs. Jaeger — which is preferred? (Tempo is simpler to operate, Jaeger has richer search)
- [ ] Is object storage available (S3-compatible, MinIO) for Tempo?
- [ ] If Jaeger — which storage backend? (ClickHouse recommended for self-hosted)

### 4.4 OpenSearch — Logs

- [ ] Log ingestion via OTLP (OTel Collector → OpenSearch exporter or Data Prepper)
- [ ] Index lifecycle management: hot → warm → cold → delete
- [ ] Index-per-service or index-per-day strategy (depends on volume)
- [ ] Integration with Grafana via OpenSearch data source (for trace ↔ log correlation)
- [ ] OpenSearch Dashboards for analytics and ad-hoc search

#### Key Questions (OpenSearch)

- [ ] Is there an existing OpenSearch cluster or does one need to be deployed from scratch?
- [ ] Expected log volume per day (GB/day)?
- [ ] Is Data Prepper (OpenSearch ingestion pipeline) needed or is direct export from OTel Collector sufficient?

### 4.5 Retention Policy

| Signal | Hot (fast access) | Warm/Cold | Notes |
|---|---|---|---|
| **Metrics** | 30 days (full resolution) | 90–365 days (downsampled) | VictoriaMetrics: built-in downsampling, `-retentionPeriod` |
| **Traces** | 7–14 days | 30 days (object storage) | For Tempo — automatic rotation to object storage |
| **Logs** | 14–30 days | 60–90 days (warm/cold tier) | OpenSearch ISM policy for automatic rotation |
| **Profiles** | 7 days | 14 days | Profiles are large, operational value is short-lived |

- [ ] Retention must be **configurable**: adjustable via configuration without code/infrastructure changes
- [ ] Different retention policies per environment (prod — longer, staging/dev — shorter)
- [ ] Monitor stored data volume and alert on quota exceedance

---

## 5. Security

### 5.1 TLS

- [ ] Encrypt traffic between all components:
  - OTel Java Agent → Collector (OTLP gRPC/HTTP with TLS)
  - Collector Agent → Collector Gateway (mTLS)
  - Collector → VictoriaMetrics, Tempo/Jaeger, OpenSearch (TLS)
  - OpAMP Supervisor → OpAMP Server (TLS)
- [ ] Automatic certificate rotation (cert-manager in OpenShift, Vault or PKI for VMs)

### 5.2 RBAC

- [ ] **Grafana RBAC**: access control for dashboards and data by team/role
  - Developers: access to their own services' data
  - SRE: full access to all data
  - Support: read-only access to dashboards and alerts
  - Management: access to SLO/SLI dashboards
- [ ] **OpenSearch Security**: role-based access to log indices (by namespace/team)
- [ ] **VictoriaMetrics**: multi-tenancy via vmauth or label-based access control
- [ ] **OTel Collector Gateway**: authentication for incoming connections (mTLS or bearer token)

### 5.3 PII Leak Detection

- [ ] **PII detection pipeline** in OTel Collector:
  - Regex-based detection: email, phone numbers, national IDs, passport numbers, card numbers
  - Mask/remove detected PII in span attributes and log body
  - Implementation via `transform` processor with regexp rules
- [ ] **PII leak monitoring**:
  - Metric for the number of masked PII fields (by type, by service)
  - Alert on anomalous spike in PII detections (potential code regression)
  - Periodic audit: sample-check data in backends for missed PII
- [ ] **Configurable rules**: PII pattern list managed centrally (via OpAMP or ConfigMap)
- [ ] **Allowlist/denylist** for span attributes and log fields

### 5.4 Key Questions

- [ ] Is PKI / cert-manager / Vault available for certificate management?
- [ ] Specific list of PII patterns to mask (domain-dependent)
- [ ] Is an audit log required for access to observability data?
- [ ] Authentication integration with existing IdP (LDAP, OIDC)?

---

## 6. Alerting

### 6.1 Alerting Architecture

```
VictoriaMetrics (vmalert)
        │
        ├── Recording rules (aggregation, precomputation)
        ├── Alerting rules (thresholds, SLO-based)
        │
        ▼
  Alertmanager
        │
        ├── Deduplication, grouping, silencing
        ├── Routing by severity and team
        │
        ▼
  Notification channels (Slack, PagerDuty, OpsGenie, email)
```

### 6.2 SLO-Based Alerting

- [ ] Define SLIs for each service:
  - **Availability**: % of successful requests (HTTP 2xx/3xx)
  - **Latency**: % of requests faster than target (p99 < X ms)
  - **Throughput**: RPS within expected range
- [ ] Burn-rate alerting (Google SRE model):
  - Fast burn: 2% error budget in 1 hour → page
  - Slow burn: 5% error budget in 6 hours → ticket
  - Example: `1 - (rate(http_requests_total{status=~"5.."}[1h]) / rate(http_requests_total[1h])) < SLO`
- [ ] SLO dashboards: error budget remaining, burn rate, SLI trends

### 6.3 Infrastructure Alerts

- [ ] **VM**: disk usage > 85%, CPU sustained > 90%, memory < 10% free, process down
- [ ] **Oracle DB**: tablespace > 80%, blocked sessions > N, long queries > X min
- [ ] **SSL**: certificate expiry < 30 / 14 / 7 days (ssl_exporter + blackbox_exporter)
- [ ] **Blackbox**: endpoint down > 2 min, latency > threshold
- [ ] **OTel Collector**: queue overflow, export errors, dropped data

### 6.4 Key Questions

- [ ] Which notification channels are needed? (Slack, PagerDuty, OpsGenie, email, MS Teams?)
- [ ] Are there existing SLOs/SLIs for current services or do they need to be defined from scratch?
- [ ] Is on-call rotation managed through Alertmanager or via an external system?
- [ ] Alert severity breakdown: critical (page) / warning (ticket) / info (dashboard)?

---

## 7. User Experience (Developer Experience)

### 7.1 For Developers

- [ ] **Zero-code start**: OTel Java Agent automatically instruments HTTP, gRPC, JDBC, Kafka, JMS
- [ ] **Custom instrumentation**: SDK libraries for custom spans and metrics (internal starter/BOM)
- [ ] **Onboarding**: documentation "how to add tracing to your service" (< 30 min)
- [ ] **Local dev**: ability to view traces locally (Jaeger all-in-one / OTLP → console exporter)
- [ ] **Grafana dashboards**: "Service Overview" template for each service:
  - RED metrics (rate, errors, duration)
  - JVM runtime (heap, GC, threads)
  - Downstream dependencies
  - Recent traces with errors

### 7.2 For Support (L2/L3)

- [ ] **Incident investigation flow**: alert → dashboard → traces → logs (single UI in Grafana)
- [ ] **Trace search**: search traces by correlation ID, user ID, request parameters
- [ ] **Log search**: full-text search in OpenSearch Dashboards
- [ ] **Runbooks**: links from alerts to diagnostic instructions

### 7.3 For SRE

- [ ] **Service map / dependency graph** based on trace data
- [ ] **Cross-service trace exploration** with attribute filtering
- [ ] **Correlation**: trace → logs → metrics in a single UI (Grafana Explore)
- [ ] **SLO dashboard**: burn rate, error budget, SLI trends across the platform
- [ ] **Capacity planning**: telemetry volume metrics, storage usage, growth forecast
- [ ] **Collector fleet management**: OpAMP UI — agent status, versions, configurations
- [ ] **Toil reduction**: automated runbook suggestions, anomaly detection (phase 2+)

### 7.4 Key Questions

- [ ] Is a shared internal library (Spring Boot starter) needed to standardize instrumentation?
- [ ] How to integrate OTel Java Agent into CI/CD and OpenShift deployment pipeline?
- [ ] Is team training required? In what format (workshop, docs, video)?
- [ ] Is there a correlation ID / request ID that passes through all services?

---

## 8. Rollout Phases (Preliminary)

> A detailed implementation plan will be prepared separately based on approved requirements.

### Phase 0 — Infrastructure and Pilot (~ 3–4 weeks)

- VictoriaMetrics: deployment, import existing recording/alerting rules
- OTel Collector: deployment on 2–3 VMs (systemd) and in 1–2 OpenShift namespaces
- OpAMP Server: basic setup, connect pilot Collectors
- Migrate exporter scraping (node, ssl, process, oracledb, blackbox) from Prometheus to OTel Collector
- Grafana: connect VictoriaMetrics data source, verify existing dashboards

### Phase 1 — Tracing + Logs (~ 4–6 weeks)

- Tempo or Jaeger: deployment
- Auto-instrumentation of 3–5 pilot services in OpenShift (OTel Java Agent)
- Context propagation across HTTP, gRPC, message broker
- OpenSearch: deployment, OTel Collector log pipeline
- Trace ↔ log correlation (trace_id in MDC)
- Basic dashboards: Service Overview, trace explorer

### Phase 2 — Scale-Out (~ 4–8 weeks)

- Roll out OTel Agent to all services in OpenShift
- Roll out OTel Collector to all VMs
- OpAMP: full fleet configuration management
- Tail-based sampling on gateway
- PII detection/masking pipeline
- SLO-based alerting (vmalert + Alertmanager)
- Log migration from Splunk → OpenSearch (gradual, per-service)

### Phase 3 — Maturity (ongoing)

- Continuous profiling (Pyroscope)
- Service map and dependency analysis
- Advanced anomaly detection
- Capacity optimization and cost monitoring
- Training and documentation

---

## 9. Non-Functional Requirements

| Requirement | Target | Notes |
|---|---|---|
| **Application overhead** | < 3% CPU, < 50 MB RAM | OTel Java Agent overhead |
| **Added latency** | < 1 ms per span export | Async export, batch processing |
| **Collector gateway availability** | 99.9% | HA: minimum 2 replicas |
| **Backend availability** | 99.9% | VictoriaMetrics, Tempo/Jaeger, OpenSearch |
| **Data loss** | < 0.1% under normal operation | Retry + persistent queue in Collector |
| **Deploy-to-visibility time** | < 2 minutes | Metrics/traces available in Grafana |
| **Scalability** | Up to 100+ services | Horizontal scaling of gateway and backends |
| **Config rollout (OpAMP)** | < 5 minutes across entire fleet | With canary strategy |
| **PII masking coverage** | 100% of spans/logs pass through transform | No bypass |

---

## 10. Open Questions and Decisions

> These questions must be resolved before implementation begins.

| # | Question | Options | Decision |
|---|---|---|---|
| Q1 | Trace backend | Tempo (simpler) / Jaeger (more mature) | **TBD** |
| Q2 | OpAMP Server | BindPlane OP / custom / OpAMP-go reference impl | **TBD** |
| Q3 | VictoriaMetrics: number of vmstorage nodes | 3 / 5 / more | **TBD** |
| Q4 | OpenSearch — new or existing | New cluster / existing cluster | **TBD** |
| Q5 | Context propagation format | W3C TraceContext / B3 | **TBD** |
| Q6 | Service discovery for VM exporters | file_sd / DNS-SD / Consul | **TBD** |
| Q7 | OTel Operator for OpenShift | Yes (auto-inject) / No (manual) | **TBD** |
| Q8 | Collector distribution | otelcol-contrib / custom build | **TBD** |
| Q9 | Object storage for Tempo | S3 / MinIO / none (Jaeger + ClickHouse) | **TBD** |
| Q10 | Notification channels for alerts | Slack / PagerDuty / OpsGenie / email / Teams | **TBD** |
| Q11 | IdP for RBAC | LDAP / OIDC / other | **TBD** |
| Q12 | Splunk → OpenSearch migration | Gradual / big bang / dual-write | **TBD** |

---

## Appendix A: Glossary

| Term | Description |
|---|---|
| **OTel** | OpenTelemetry — CNCF project, open standard for telemetry collection |
| **Collector** | OTel Collector — agent/gateway for receiving, processing, and exporting telemetry |
| **OpAMP** | Open Agent Management Protocol — protocol for centralized agent (Collector) management |
| **Span** | A unit of work in a distributed trace (method call, HTTP request, SQL query) |
| **RED** | Rate, Errors, Duration — key metrics for microservices |
| **MDC** | Mapped Diagnostic Context — Log4j2/Logback mechanism for adding context to logs |
| **SLO/SLI** | Service Level Objective / Indicator — target service quality metrics |
| **Burn rate** | Rate of error budget consumption — the basis of SLO-based alerting |
| **Tail-based sampling** | Decision to retain a trace is made after all spans complete (based on error, latency) |
| **VictoriaMetrics** | OSS TSDB, Prometheus-compatible, optimized for high cardinality and long-term storage |
| **OpenSearch** | OSS Elasticsearch fork for logs and full-text search |
| **Pyroscope** | Continuous profiling platform (Grafana), JVM support |
