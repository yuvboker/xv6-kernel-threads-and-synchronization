#include "types.h"
#include "user.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "spinlock.h"
#include "tournament_tree.h"

int release_lock(trnmnt_tree* tree, int parent);

trnmnt_tree* trnmnt_tree_alloc(int depth){
  int i;
  int nodes = 1;
  int threads;
  for(i=0;i<=depth;i++){
    nodes *= 2;
  }
  threads = nodes/2;
  nodes--;
  int lock = kthread_mutex_alloc();
  kthread_mutex_lock(lock);
  trnmnt_tree *tree = malloc(sizeof(trnmnt_tree));
  tree->lock = lock;
  int* array_of_locks = malloc(nodes*4);
  int* array_of_taken_ids = malloc(threads*4);
  tree->threads = threads;
  tree->depth = depth;
  tree->total_nodes = nodes;
  //printf(1,"threads: %d depth: %d total_nodes: %d\n", threads,depth,nodes);
  for(i=0;i<nodes;i++){
    array_of_locks[i]=kthread_mutex_alloc();
  }
  tree->nodes = array_of_locks;
  tree->taken_ids = array_of_taken_ids;
  kthread_mutex_unlock(tree->lock);
  return tree;
}
int trnmnt_tree_dealloc(trnmnt_tree* tree){
  if(!tree)
    return -1;
  int i;
  kthread_mutex_lock(tree->lock);
  if(tree->num_of_locked){
    kthread_mutex_unlock(tree->lock);
    return -1;
  }
  int nodes = tree->total_nodes; 
  for(i=0;i<nodes;i++){
      kthread_mutex_dealloc(tree->nodes[i]);
  }
  tree->killed = 1;
  free(tree->nodes);
  free(tree->taken_ids);
  free(tree);
  kthread_mutex_unlock(tree->lock);
  return 0;
}
int trnmnt_tree_acquire(trnmnt_tree* tree,int ID){
  if(!tree)
    return -1;
  if(ID>tree->threads-1)
    return -1;
  //printf(1,"got here\n" );
  kthread_mutex_lock(tree->lock);
  if(tree->taken_ids[ID]){
    kthread_mutex_unlock(tree->lock);
    return -1;
  }
  //printf(1,"got here\n" );
  tree->taken_ids[ID]=1;
  tree->num_of_locked++;
  kthread_mutex_unlock(tree->lock);
  int real_index_in_tree = tree->total_nodes-tree->threads + ID;
  int parent = real_index_in_tree;
  for(int i=0;i<tree->depth;i++){
      parent = (parent-1)/2;
      if(kthread_mutex_lock(tree->nodes[parent]))
          return -1;
  } 
  return 0;
}
int trnmnt_tree_release(trnmnt_tree* tree,int ID){
  if(!tree)
    return -1;
  if(ID>tree->threads-1)
    return -1;

  kthread_mutex_lock(tree->lock);
  if(!tree->taken_ids[ID]){
    kthread_mutex_unlock(tree->lock);
    return -1;
  }
  kthread_mutex_unlock(tree->lock);
  int real_index_in_tree = tree->total_nodes-tree->threads + ID;
  int parent = (real_index_in_tree-1)/2;
  int success = release_lock(tree, parent);
  if(success){
    return -1;
  }
  kthread_mutex_lock(tree->lock);
  tree->taken_ids[ID]=0;
  tree->num_of_locked--;
  kthread_mutex_unlock(tree->lock);
  return 0;
}

int release_lock(trnmnt_tree* tree, int parent){
   if(!parent)
      return kthread_mutex_unlock(tree->nodes[0]);
   return release_lock(tree,(parent-1)/2) + kthread_mutex_unlock(tree->nodes[parent]);
}