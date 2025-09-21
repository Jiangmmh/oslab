#include <string.h>
#include <errno.h>
#include <asm/segment.h>

// 这里在内核中，kernelname是内核中的一个全局buffer
char kernelname[24];
int sys_iam(const char * name)
{   
    char c;
    int i = 0;
    
    while ((c = get_fs_byte(name + i)) != '\0') {
        kernelname[i] = c;
        ++i;
    }

    if (i > 23) {
        errno = EINVAL;
        return -1;
    }
    
    printk("lab2-iam string: %s\n", kernelname);
    return 0;
}

int sys_whoami(char* name, unsigned int size)
{
    if (size < strlen(kernelname)) {
        errno = EINVAL;
        return -1;
    }

    int i = 0;
    char c;
    while ((c = kernelname[i]) != '\0') {
        put_fs_byte(c, name++);
        i++;
    }

    printk("lab2-whoami string: %s\n", kernelname);
    return 0;
}