/* ARMCC标准C库重定向：不再通过BKPT 0xAB请求调试器提供文件服务。
 * 只支持stdin/stdout/stderr，不提供磁盘文件系统。标准输出交给现有UART1。
 * C库会在main之前打开这三个流，因此_sys_open里不能访问尚未初始化的UART。
 */
#include <rt_sys.h>
#include <string.h>
#include "uart.h"

#pragma import(__use_no_semihosting)

const char __stdin_name[] = "STDIN";
const char __stdout_name[] = "STDOUT";
const char __stderr_name[] = "STDERR";

FILEHANDLE _sys_open(const char *name, int mode)
{
    (void)mode;
    if (!strcmp(name, __stdin_name))
        return 0;
    if (!strcmp(name, __stdout_name))
        return 1;
    if (!strcmp(name, __stderr_name))
        return 2;
    return -1;
}
int _sys_close(FILEHANDLE handle)
{
    (void)handle;
    return 0;
}
int _sys_istty(FILEHANDLE handle)
{
    return handle >= 0 && handle <= 2;
}
int _sys_write(FILEHANDLE handle, const unsigned char *buf, unsigned len, int mode)
{
    unsigned i;
    (void)mode;
    if (handle != 1 && handle != 2)
        return -1;
    for (i = 0; i < len; ++i)
        UART1_SendByte(buf[i]);
    return 0; /* ARM库约定：返回未写出的字节数，而不是已写出的长度。 */
}
int _sys_read(FILEHANDLE handle, unsigned char *buf, unsigned len, int mode)
{
    (void)handle;
    (void)buf;
    (void)mode;
    return (int)len; /* 本项目不使用scanf；立即返回无数据，不阻塞应用。 */
}
int _sys_seek(FILEHANDLE handle, long pos)
{
    (void)handle;
    (void)pos;
    return -1;
}
long _sys_flen(FILEHANDLE handle)
{
    (void)handle;
    return -1;
}
int _sys_ensure(FILEHANDLE handle)
{
    (void)handle;
    return 0;
}
int _sys_tmpnam(char *name, int sig, unsigned len)
{
    (void)name;
    (void)sig;
    (void)len;
    return 0;
}
char *_sys_command_string(char *cmd, int len)
{
    (void)cmd;
    (void)len;
    return 0;
}
void _ttywrch(int ch)
{
    UART1_SendByte((u8)ch);
}
void _sys_exit(int code)
{
    (void)code;
    for (;;)
    {
    } /* main不应返回；退出也绝不能依赖调试器。 */
}
