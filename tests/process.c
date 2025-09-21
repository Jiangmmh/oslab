#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include <sys/wait.h>
#include <stdlib.h>

#define TICKS_PER_SECOND sysconf(_SC_CLK_TCK)

void cpuio_bound(int last, int cpu_time, int io_time);

int main(int argc, char * argv[])
{
    int pid;

    if ((pid = fork()) == -1) {
        perror("fork() failed.\n");
        exit(1);
    }

    if (pid != 0) {
        printf("child process1: %d\n", pid);
        if ((pid = fork()) == -1) {
            perror("fork() failed.\n");
            exit(1);
        }

        if (pid != 0) {
            printf("child process2: %d\n", pid);
            if ((pid = fork()) == -1) {
                perror("fork() failed.\n");
                exit(1);
            }

            if (pid != 0) {
                printf("child process3: %d\n", pid);
                if ((pid = fork()) == -1) {
                    perror("fork() failed.\n");
                    exit(1);
                }
                if (pid != 0) {
                    printf("child process4: %d\n", pid);
                    wait(0);
                    wait(0);
                    wait(0);
                    wait(0);
                    printf("All process done!\n");
                } else {
                    cpuio_bound(10, 1, 9);
                }
            } else {
                cpuio_bound(10, 1, 1);
            }
        } else {
            cpuio_bound(10, 0, 1);
        }
    } else {
        cpuio_bound(10, 1, 0);
    }

	return 0;
}

/*
 * 此函数按照参数占用CPU和I/O时间
 * last: 函数实际占用CPU和I/O的总时间，不含在就绪队列中的时间，>=0是必须的
 * cpu_time: 一次连续占用CPU的时间，>=0是必须的
 * io_time: 一次I/O消耗的时间，>=0是必须的
 * 如果last > cpu_time + io_time，则往复多次占用CPU和I/O
 * 所有时间的单位为秒
 */
void cpuio_bound(int last, int cpu_time, int io_time)
{
	struct tms start_time, current_time;  // 包含该进程和子进程的用户执行时间和系统执行时间
	clock_t utime, stime;
	int sleep_time;

	while (last > 0)
	{
		/* CPU Burst */
		times(&start_time);  // 调用time，将当前时间填入start_time
		/* 其实只有t.tms_utime才是真正的CPU时间。但我们是在模拟一个
		 * 只在用户状态运行的CPU大户，就像“for(;;);”。所以把t.tms_stime
		 * 加上很合理。*/
		do
		{
			times(&current_time);
			utime = current_time.tms_utime - start_time.tms_utime;  // 获取从开始到现在的用户执行时间
			stime = current_time.tms_stime - start_time.tms_stime;  // 获取从开始到现在的系统执行时间
		} while ( ( (utime + stime) / TICKS_PER_SECOND )  < cpu_time );
		last -= cpu_time;

		if (last <= 0 )
			break;

		/* IO Burst */
		/* 用sleep(1)模拟1秒钟的I/O操作 */
		sleep_time=0;
		while (sleep_time < io_time)
		{
			sleep(1);
			sleep_time++;
		}
		last -= sleep_time;
	}
}