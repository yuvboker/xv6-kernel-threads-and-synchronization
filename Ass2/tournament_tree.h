struct spinlock;

struct trnmnt_tree{
	int depth; // 
	int threads; // 2^depth
	int total_nodes;
	int killed;
	int num_of_locked;
	int lock;
	int *taken_ids;
	int *nodes;//2^(depth+1)-1
};

typedef struct trnmnt_tree trnmnt_tree;
/*
struct node{
	int lock;
	int thread;
};
typedef struct node node;
*/
//tournament.c
trnmnt_tree* trnmnt_tree_alloc(int);
int trnmnt_tree_dealloc(trnmnt_tree* );
int trnmnt_tree_acquire(trnmnt_tree* ,int);
int trnmnt_tree_release(trnmnt_tree* ,int);