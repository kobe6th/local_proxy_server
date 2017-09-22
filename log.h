#ifndef LOG_H_
#define LOG_H_

#define DEBUG_LINE() printf("[%s:%s] line=%d\r\n",__FILE__, __func__, __LINE__)
#define DEBUG_ERR(fmt, args...) printf("\033[46;31m[%s:%d]\033[0m "#fmt" errno=%d, %m\r\n", __FILE__, __LINE__, ##args, errno, errno)
#define DEBUG_INFO(fmt, args...) printf("\033[33m[%s:%d]\033[0m "#fmt"\r\n", __FILE__, __LINE__, ##args)

#endif/*LOG_H_*/