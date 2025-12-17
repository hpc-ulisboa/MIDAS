int main() {
    int i, sum;
    volatile int x[100], y[100], z[100];
    sum = 0;
  #pragma DFGLoop loop
    for (i = 0; i < 100; i++) {
        z[i] = x[i] * 50 + y[i];
        //sum += c[i]; 
    }
    return 0;
  }
  