#include <kernel/console.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/uart.h>
#include <common/string.h>

struct console cons;
#define BUF_NXT(x) (((x)+1)%IBUF_SIZE)
#define BUF_PRE(x) (((x)+IBUF_SIZE-1)%IBUF_SIZE)

static char cmd_buf[CMD_BUF_SIZE][IBUF_SIZE];
static int cmd_base=0,cmd_cur=0;
#define CMD_BUF_NXT(x) (((x)+1)%CMD_BUF_SIZE)
#define CMD_BUF_PRE(x) (((x)+CMD_BUF_SIZE-1)%CMD_BUF_SIZE)

void console_init()
{
    /* (Final) TODO BEGIN */
    init_spinlock(&cons.lock);
    init_sem(&cons.sem,0);
    /* (Final) TODO END */
}

/**
 * console_write - write to uart from the console buffer.
 * @ip: the pointer to the inode
 * @buf: the buffer
 * @n: number of bytes to write
 */
isize console_write(Inode *ip, char *buf, isize n)
{
    /* (Final) TODO BEGIN */
    inodes.unlock(ip);
    acquire_spinlock(&cons.lock);
    for(int i=0;i<n;i++){
        uart_put_char(buf[i]&0xff);
    }
    release_spinlock(&cons.lock);
    inodes.lock(ip);
    return n;
    /* (Final) TODO END */
}

/**
 * console_read - read to the destination from the buffer
 * @ip: the pointer to the inode
 * @dst: the destination
 * @n: number of bytes to read
 */
isize console_read(Inode *ip, char *dst, isize n)
{
    /* (Final) TODO BEGIN */
    inodes.unlock(ip);
    acquire_spinlock(&cons.lock);
    isize target=n,r=0;
    while(n>0){
        if(cons.read_idx==cons.write_idx){
            release_spinlock(&cons.lock);
            if(wait_sem(&cons.sem)==0)return -1;
            acquire_spinlock(&cons.lock);
        }
        cons.read_idx=BUF_NXT(cons.read_idx);
        char c=cons.buf[cons.read_idx];
        if(c==C('D')){
            if(n<target)cons.read_idx=BUF_PRE(cons.read_idx);
            break;
        }
        *(dst++)=c;
        r++;
        n--;
        if(c=='\n')break;
    }
    release_spinlock(&cons.lock);
    inodes.lock(ip);
    return r;
    /* (Final) TODO END */
}

void back_buf_clear(){
    while(cons.back_size!=0){
        cons.edit_idx=BUF_NXT(cons.edit_idx);
        cons.buf[cons.edit_idx]=cons.back_buf[--cons.back_size];
        uart_put_char(cons.back_buf[cons.back_size]);
    }   
}

void clear_line(){
    back_buf_clear();
    while(cons.edit_idx!=cons.write_idx&&cons.buf[cons.edit_idx]!='\n'){
        cons.edit_idx=BUF_PRE(cons.edit_idx);
        uart_put_char('\b');
        uart_put_char(' ');
        uart_put_char('\b');
    }
}

void write_back(int cmd_idx){
    int p=0;
    for(usize i=cons.write_idx;i!=cons.edit_idx;i=BUF_NXT(i)){
        if(cons.buf[i]=='\n'||cons.buf[i]=='\0')continue;
        cmd_buf[cmd_idx][p++]=cons.buf[i];
    }
    usize i=cons.edit_idx;
    if(!(cons.buf[i]=='\n'||cons.buf[i]=='\0'))cmd_buf[cmd_idx][p++]=cons.buf[i];
    cmd_buf[cmd_idx][p]='\0';
}

void console_intr(char c)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&cons.lock);
    if(c==C('D')){
        if(BUF_NXT(cons.edit_idx)!= cons.read_idx){
            back_buf_clear();
            cons.edit_idx = BUF_NXT(cons.edit_idx);
            cons.buf[cons.edit_idx] = c;
            uart_put_char(c);
            cons.write_idx = cons.edit_idx;
            post_sem(&cons.sem);
        }
    }
    else if(c==C('U')){
        clear_line();
    }
    else if(c=='\x7f'){
        if(cons.edit_idx!=cons.write_idx){
            cons.edit_idx=BUF_PRE(cons.edit_idx);
            uart_put_char('\b');
            uart_put_char(' ');
            uart_put_char('\b');
        }
    }
    else{
        if(c=='\r')c='\n';
        if (BUF_NXT(cons.edit_idx)!=cons.read_idx){
            if(c!='\n'){
                cons.edit_idx=BUF_NXT(cons.edit_idx);
                cons.buf[cons.edit_idx]=c;
                uart_put_char(c);
            }
            if (c=='\n'||BUF_NXT(cons.edit_idx+cons.back_size)==cons.read_idx){
                back_buf_clear();
                if(c=='\n'){
                    cons.edit_idx=BUF_NXT(cons.edit_idx);
                    cons.buf[cons.edit_idx]=c;
                    uart_put_char(c);
                }
                
                write_back(cmd_base);
                if(strncmp(
                    cmd_buf[cmd_base],
                    cmd_buf[CMD_BUF_PRE(cmd_base)],
                    MAX(strlen(cmd_buf[cmd_base]),strlen(cmd_buf[CMD_BUF_PRE(cmd_base)])))==0);
                else if(cmd_buf[cmd_base][0]=='\0');
                else{
                    cmd_base=CMD_BUF_NXT(cmd_base);
                }
                cmd_buf[cmd_base][0]='\0';
                cmd_cur=cmd_base;

                cons.write_idx=cons.edit_idx;
                post_sem(&cons.sem);
            }
        }
    }
    for(int i=(int)cons.back_size-1;i>=0;i-=1)uart_put_char(cons.back_buf[i]);
    uart_put_char(' ');
    for(usize i=0;i<=cons.back_size;i++)uart_put_char('\b');

    release_spinlock(&cons.lock);
    /* (Final) TODO END */
}
void console_intr_arror(char c){
    acquire_spinlock(&cons.lock);
    if(c=='A'){//UP
        if(CMD_BUF_PRE(cmd_cur)==cmd_base||cmd_buf[CMD_BUF_PRE(cmd_cur)][0]=='\0'){
            release_spinlock(&cons.lock);
            return;
        }
        write_back(cmd_cur);
        clear_line();
        cmd_cur=CMD_BUF_PRE(cmd_cur);
        for(int i=0;cmd_buf[cmd_cur][i]!='\0';i++){
            cons.edit_idx=BUF_NXT(cons.edit_idx);
            cons.buf[cons.edit_idx]=cmd_buf[cmd_cur][i];
            uart_put_char(cmd_buf[cmd_cur][i]);
        }
    }
    else if(c=='B'){//DOWN
        if(CMD_BUF_NXT(cmd_cur)==cmd_base||cmd_cur==cmd_base){
            release_spinlock(&cons.lock);
            return;
        }
        write_back(cmd_cur);
        clear_line();
        cmd_cur=CMD_BUF_NXT(cmd_cur);
        for(int i=0;cmd_buf[cmd_cur][i]!='\0';i++){
            cons.edit_idx=BUF_NXT(cons.edit_idx);
            cons.buf[cons.edit_idx]=cmd_buf[cmd_cur][i];
            uart_put_char(cmd_buf[cmd_cur][i]);
        }
    }
    else if(c=='D'){//LEFT
        if(cons.edit_idx==cons.write_idx){
            release_spinlock(&cons.lock);
            return;
        }
        cons.back_buf[cons.back_size++]=cons.buf[cons.edit_idx];
        cons.edit_idx=BUF_PRE(cons.edit_idx);
        uart_put_char('\b');
    }
    else if(c=='C'){//RIGHT
        if(cons.back_size==0){
            release_spinlock(&cons.lock);
            return;
        }
        uart_put_char(cons.back_buf[--cons.back_size]);
        cons.edit_idx=BUF_NXT(cons.edit_idx);
        cons.buf[cons.edit_idx]=cons.back_buf[cons.back_size];
    }
    else{
        release_spinlock(&cons.lock);
        console_intr('^');
        console_intr('[');
        console_intr(c);
        acquire_spinlock(&cons.lock);
    }
    release_spinlock(&cons.lock);
}