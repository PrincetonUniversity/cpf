#ifndef RESTEER_H

typedef void (*RecoverFun)(void);
typedef void (*ContFun)(void);

void enableResteer(RecoverFun recover, ContFun cont);
void resteer(pid_t pid);

#endif /* RESTEER_H */
