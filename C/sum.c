#include <stdio.h>

int sum(int a, int b) { return a + b; }

int main() {
  int result = sum(33, 1000);
  printf("sum is=%d\n", result);
}