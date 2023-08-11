//
// Created by luxingzhi on 23-7-11.
//

#include "kernel/types.h"

#include "kernel/stat.h"
#include "user/user.h"

void f(int *left_pipe) {
  int pid = fork();
  if (pid == 0) {
    // 子进程
    int p;
    if (read(left_pipe[0], &p, sizeof(p)) == 0) {
      close(left_pipe[0]);
      return;
    }
    printf("prime %d\n", p);

    int right_pipe[2];
    pipe(right_pipe);

    while (1) {
      int n;
      int flag = read(left_pipe[0], &n, sizeof(n));
      if (n % p != 0) {
        write(right_pipe[1], &n, sizeof(n));
      }
      if (flag == 0)
        break;
    }
    close(left_pipe[0]);
    close(right_pipe[1]);
    f(right_pipe);
  } else {
    int st;
    wait(&st);
  }
}

int main() {
  int p[2];
  pipe(p);
  for (int i = 2; i <= 35; ++i) {
    write(p[1], &i, sizeof(i));
  }
  close(p[1]);
  f(p);
  exit(0);
}
