#include <kernel/console.h>
#include <aarch64/intrinsic.h>
#include <kernel/sched.h>
#include <driver/uart.h>

struct console cons;
#define BUF_NXT(x) (((x)+1)%IBUF_SIZE)
#define BUF_PRE(x) (((x)+IBUF_SIZE-1)%IBUF_SIZE)

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

void console_intr(char c)
{
    /* (Final) TODO BEGIN */
    acquire_spinlock(&cons.lock);
    if(c==C('D')){
        if(BUF_NXT(cons.edit_idx)!= cons.read_idx){
            cons.edit_idx = BUF_NXT(cons.edit_idx);
            cons.buf[cons.edit_idx] = c;
            uart_put_char(c);
            cons.write_idx = cons.edit_idx;
            post_sem(&cons.sem);
        }
    }
    else if(c==C('U')){
        while(cons.edit_idx!=cons.write_idx&&cons.buf[BUF_PRE(cons.edit_idx)]!='\n'){
            cons.edit_idx=BUF_PRE(cons.edit_idx);
            uart_put_char('\b');
            uart_put_char(' ');
            uart_put_char('\b');
        }
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
            cons.edit_idx=BUF_NXT(cons.edit_idx);
            cons.buf[cons.edit_idx]=c;
            uart_put_char(c);
            if (c=='\n'||BUF_NXT(cons.edit_idx)==cons.read_idx){
                cons.write_idx=cons.edit_idx;
                post_sem(&cons.sem);
            }
        }
    }
    release_spinlock(&cons.lock);
    /* (Final) TODO END */
}