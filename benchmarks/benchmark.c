int main() {
    int i;
    volatile int x[5] = {1, 2, 3, 4, 5}, y[5] = {2, 1, 2, 3, 2}, z[5];
  #pragma DFGLoop loop
    for (i = 0; i < 5; i++) {
      z[i] = 5 * x[i] + y[i];  
    }
    return 0;
  }
  
