// test_program.c
#include <stdio.h>

void sayHello() { printf("Hello from sayHello!\n"); }

int add(int a, int b) { return a + b; }

int main() {
  sayHello();
  int result = add(10, 20);
  printf("Result of add: %d\n", result);
  return 0;
}
