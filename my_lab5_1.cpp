#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>

#include "cachelab.h"

// opt变量，定义cache大小
int verbose=0;
int s, S, E, b, B;

// 全局时刻表
int T = 0;

// 该linux机器是64位的，uint64_t被typedef为unsigned long，自然是用%lx读入

// 定义cache行，在本次实验中不需要存block，以行为单位操作
typedef struct cacheLine{
    int t;  // 时刻（表示这个行在t时刻被更新），也是valid位
    uint64_t tag; // 标记
} * cacheGroup; // 定义cache组，即行的数组

cacheGroup *cache;// 定义cache，即组的数组

enum Category
{
    HIT,
    MISS,
    EVICTION
};

const char *categoryString[3] = {"hit ", "miss ", "eviction "};

// 保存统计结果
int result[3];// 表示HIT/MISS/EVICTION的次数

// 处理命令行选项
FILE *opt(int argc, char **argv){
    FILE *tracefile;
    int opt;
    while ((opt = getopt(argc, argv, "hvsEbt")) != EOF){// EOF就是-1
        switch(opt){
            case 'h':
                // 输出帮助文档
                exit(1);
                break;
            case 'v':
                // 输出详细运行过程信息
                verbose = 1;
                break;
            case 's':
                // s是组索引的位数
                s = atoi(argv[optind]);// 获取-s选项的参数，atoi将字符串转为数字（C++中是stoi）
                if(s <= 0)
                    exit(1);
                S = 1 << s;// S就是有多少组数
                break;
            case 'E':
                // 每个cache组的行数
                E = atoi(argv[optind]);
                if(E <= 0)
                    exit(1);
                break;
            case 'b':
                // 块内地址位数
                b = atoi(argv[optind]);
                if(b <= 0)
                    exit(1);
                B = 1 << b;// 块的大小
                break;
            case 't':// -t traces/yi.trace
                // 输入数据文件的路径
                tracefile = fopen(argv[optind], "r");
                if(tracefile == NULL)
                    exit(1);
                break;
        }
    }
    return tracefile;
}

// 初始化cache
void init_Cache(){
    //cache 有S组，每组有E行
    cache = (cacheGroup *)malloc(S * sizeof(cacheGroup));
    for (int i = 0; i < S; i++){
        cache[i] = (struct cacheLine *)malloc(E * sizeof(struct cacheLine));
        for (int j = 0; j < E; j++){
            cache[i][j].t = 0;
        }
    }
}

// 更新cache
// 更新哪一个组的哪一个行
void update_Cache(cacheGroup group, int line_idx, enum Category category, uint64_t tag, char *resultV){
    if(line_idx != -1){// 行索引是否合法
        group[line_idx].t = T;
        group[line_idx].tag = tag;
    }
    // category=0表示表示HIT，=1表示MISS，=2表示EVICTION
    result[category]++;// 记录HIT/MISS/EVICTION的次数
    if(verbose)// 需要记录详细信息
        strcat(resultV, categoryString[category]);
}


// 查找cache
// tag用来判断是否命中，group_idx表示在哪一个组中查询，resultV表示要输出的详细信息
void search_Cache(uint64_t addr_tag, int group_idx, char *resultV){
    cacheGroup group = cache[group_idx];
    // min_t_idx用来淘汰最久未访问的行, empty_line_idx用来记录空行
    int min_t_idx = 0, empty_line_idx = -1;
    for (int i = 0; i < E; i++){// 枚举每一行
        struct cacheLine line = group[i];// 必须要用struct cacheLine，不能cacheLine
        if(line.t){ // 行非空
            if(line.tag == addr_tag){ // 标记相等表示命中，HIT命中
                update_Cache(group, i, HIT, addr_tag, resultV);
                return;
            }
            // 未命中的时候，记录最早被更新的行
            if(group[min_t_idx].t > line.t){
                min_t_idx = i;
            }
        }
        else{// 找到了空行
            empty_line_idx = i;
        }
    }
    // MISS没有命中
    // 没找到空行的时候会传入非法的行索引-1
    update_Cache(group, empty_line_idx, MISS, addr_tag, resultV);
    // EVICTION需要淘汰
    if(empty_line_idx == -1){// 没有空行的时候需要淘汰
        update_Cache(group, min_t_idx, EVICTION, addr_tag, resultV);
    }
}
/*
L 110,1 miss eviction 
L 210,1 miss eviction 
M 12,1 miss eviction hit 

一次内存访问可能同时触发MISS 和 EVICTION
*/ 


void destory(){
    for (int i = 0; i < S; i++){// 一共S组
        free(cache[i]);
    }
    free(cache);
}

int main(int argc, char **argv){
    FILE *tracefile = opt(argc, argv);
    init_Cache();

    // 记录文件中每一条指令的类型、存储器地址、访问的字节数
    char operation;
    uint64_t addr;
    int size;// 访问的内存字节数量，本实验内存访问的地址总是正确对齐的，可忽略该变量
    // tracefile是内存访问轨迹文件
    // %x表示16进制的int，%lx表示16进制的long，%llx表示16进制的longlong(64bit)
    while(fscanf(tracefile, " %c %lx,%d\n", &operation, &addr, &size) == 3){
        if(operation == 'I')// I表示指令装载，L表示表示数据装载，S数据存储
            continue;
        // 最低的b位是块内偏移，中间的s位是组索引，剩余的最高位是标志位
        // 取出s位的组索引
        int group_idx = (addr >> b) & ~(~0u << s);
        // 取出存储器地址的标志位
        uint64_t addr_tag = addr >> (b + s);
        // 全局时刻表自增1
        T++;
        // 存储-v的输出信息
        char resultV[10];// 存储这一次操作的状态，是HIT/MISS/EVICTION
        memset(resultV, 0, sizeof(resultV));
        search_Cache(addr_tag, group_idx, resultV);
        // M操作需要内存访问两次
        if(operation == 'M'){// 数据修改（即数据装载后接数据存储）
            search_Cache(addr_tag, group_idx, resultV);
        }
        if(verbose){
            fprintf(stdout, "%c %lx,%d %s\n", operation, addr, size, resultV);
        }
    }
    // 命中次数，不命中次数，淘汰次数
    printSummary(result[0], result[1], result[2]);
    destory();
    return 0;
}