#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <math.h>
#include "memlib.h"
#include "mm.h"

#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 8
#define DSIZE 16
#define CHUNKSIZE (1 << 12)
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))
#define GET(p) (*(uintptr_t *)(p))
#define PUT(p, val) ((*(uintptr_t *)(p)) = (uintptr_t)(val))
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_HEAD(num) ((char *)(GET(heap_list + WSIZE * (num))))
#define PRE_N(bp) ((char *)(GET(bp)))
#define SUC_N(bp) ((char *)(GET((char *)(bp) + WSIZE)))
#define GET_PTR(p) ((char *)(GET(p)))
#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))

#define LIST_NUM 20
void delete(void *bp);//删除链表中的节点
void *coalesce(void *bp);//合并相邻空闲块
void *extend_heap(size_t words);//扩展堆
char *find_head(size_t size);//找链头地址
void *search(size_t size);//找有没有合适的空闲块能分配出去
void insert(void *bp);//把空闲块插入链表
void cut(void *bp,size_t size);//把空闲块切成合适的大小
int ipow(int a,int b);//参数是int的幂函数

static char *heap_listp=NULL;
int ipow(int a,int b)//用来算2的幂的函数，其实可以用左移但是我忘了
{
	int ans=1;
	for(int i=1;i<=b;i++)
		ans*=a;
	return ans;
}

void delete(void *bp)
{
	size_t size=GET_SIZE(HDRP(bp));
	if(PRE_N(bp)==NULL&&SUC_N(bp)!=NULL)//如果有前面没有后面：最后一个  把前面的SUC改成NULL
	{
		PUT(find_head(size),SUC_N(bp));
		PUT(SUC_N(bp),NULL);
	}
	if(PRE_N(bp)==NULL&&SUC_N(bp)==NULL)//如果前后都没有：既是第一个也是最后一个，说明只有一个  把头改成NULL
	{
		PUT(find_head(size),NULL);
	}
	if(PRE_N(bp)!=NULL&&SUC_N(bp)==NULL)//前面没有后面有：第一个    把头改成指向第二个
	{
		PUT(PRE_N(bp)+WSIZE,NULL);
	}
	if(PRE_N(bp)!=NULL&&SUC_N(bp)!=NULL)//前后都有：在中间（大部分情况）    把前面的SUC指向后面，后面的PRE指向前面
	{
		PUT(PRE_N(bp)+WSIZE,SUC_N(bp));
		PUT(SUC_N(bp),PRE_N(bp));
	}
}
void insert(void *bp)//插入空闲链表头
{
	size_t size=GET_SIZE(HDRP(bp));
	if((void *)GET(find_head(size))==NULL)//如果表头是空的，说明将要成为第一个，则bp没有PRE，表头填bp
	{
		PUT(find_head(size),bp);
		PUT(bp,NULL);
		PUT(bp+WSIZE,NULL);			//change1:DSIZE->WSIZE
	}
	else//如果表头不空：那就变成第一个，bp填入表头，bp填入原第一个的PRE，bp的SUC填原第一个，PRE是NULL
	{
		PUT(bp,NULL);
		PUT((char *)bp+WSIZE,GET(find_head(size)));
		PUT(GET(find_head(size)),bp);//change2:find_head->find_head(size)
		PUT(find_head(size),bp);//change3:change line 110 111
	}
}
void *coalesce(void *bp) {//合并空闲块 由于合并还是相邻合并所以几乎就是教材的版本，除了从链表里删除被合并的节点和合并之后要放进链表
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {//前后都在用 啥都不合并直接放进链表结束
		insert(bp);//change7:   ->insert(bp);
        return bp;
    } else if (prev_alloc && !next_alloc) {//后面是空闲，合并后面
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        delete(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    } else if (!prev_alloc && next_alloc) {//前面是空的，合并前面，地址变成前面的
    	delete(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    } else {//前后都是空的，全合并
    	delete(PREV_BLKP(bp));
    	delete(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    insert(bp);//change4:  ->insert(bp);//合并完了放进链表
    return bp;
}
void *extend_heap(size_t words){//教材版本
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk((int)size)) == (void *)-1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}
int mm_init(void) {		//容纳20个链表头+创建序言块+链表头初始化+拓展堆
	if((heap_listp=mem_sbrk((LIST_NUM+4)*WSIZE))==(void *)-1)
		return -1;
	PUT(heap_listp+(WSIZE*(LIST_NUM+1)),PACK(DSIZE,1));
	PUT(heap_listp+(WSIZE*(LIST_NUM+2)),PACK(DSIZE,1));
	PUT(heap_listp+(WSIZE*(LIST_NUM+3)),PACK(0,1));
	for(int i=0;i<LIST_NUM;i++)
	{
		PUT(heap_listp+i*WSIZE,NULL);
	}
	if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
	    return -1;
	return 0;
}
char *find_head(size_t size)//找对应的链头
{
	for(int i=5;i<5+LIST_NUM;i++)
	{
		if(ipow(2,i)>=size) //按照2的次方分组，最低32字节
		{
			return heap_listp+(i-5)*WSIZE;//change5:DSIZE->WSIZE
		}
	}
	return heap_listp+19*WSIZE;//change6:DSIZE->WSIZE //如果过大也在最后一个链表
}
void *search(size_t size)//根据大小找合适的空闲块,如果没有返回NULL
{
	char *p=find_head(size);
	char *bp=p;
	while(bp<=find_head(ipow(2,24)))//换下一个头
	{
		for(bp=(char *)GET(bp);bp!=NULL;bp=SUC_N(bp))//表内遍历
		{
			if(GET_SIZE(HDRP(bp))>=size) return bp;
		}
		p+=WSIZE;
		bp=p;
	}
	return NULL;
}
void *mm_malloc(size_t size) {
	if(size==0) return NULL;
	size_t rsize=DSIZE*((size+2*DSIZE-1)/DSIZE);//change8:DSIZE*(size+2*DSIZE-1)/DSIZE -> DSIZE*((size+2*DSIZE-1)/DSIZE)
	char *bp=search(rsize);//先找找有没有空闲的
	if(bp!=NULL)//有就切出一块合适的大小
	{
		cut(bp,rsize);
		return bp;
	}
	else//没有就开一页新的，切出一块合适的
	{
		if((bp=extend_heap(MAX(rsize,CHUNKSIZE)/WSIZE))==NULL) return NULL;
		cut(bp,rsize);
		return bp;
	}
}
void cut(void *bp,size_t size)
{
	delete(bp);
	size_t pre_size=GET_SIZE(HDRP(bp));
	if(GET_SIZE(HDRP(bp))-size>=2*DSIZE)//必须得够大
	{
		PUT(HDRP(bp),PACK(size,1));
		PUT(FTRP(bp),PACK(size,1));
		bp=NEXT_BLKP(bp);
		PUT(HDRP(bp),PACK(pre_size-size,0));
		PUT(FTRP(bp),PACK(pre_size-size,0));
		insert(bp);
	}
	else//不够大就不切了直接用
	{
		PUT(HDRP(bp),PACK(pre_size,1));
		PUT(FTRP(bp),PACK(pre_size,1));
	}

}
void mm_free(void *ptr) {
	if(ptr==NULL) return;//free(NULL)应当是没有作用的
	PUT(HDRP(ptr),PACK(GET_SIZE(HDRP(ptr)),0));
	PUT(FTRP(ptr),PACK(GET_SIZE(HDRP(ptr)),0));
	coalesce(ptr);
}

void *mm_realloc(void *ptr, size_t size){
	if(size==0) {mm_free(ptr);return NULL;}//特判
	if(ptr==NULL) return mm_malloc(size);//特判
	char *bp;
	if((bp=mm_malloc(size))==NULL) return NULL;
	size_t copysize = GET_SIZE(HDRP(ptr)) - 2*WSIZE;
	    if(size < copysize)
	        copysize = size;
	memcpy(bp,ptr,copysize);
	mm_free(ptr);
	return bp;
}