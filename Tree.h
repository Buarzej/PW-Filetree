#pragma once

typedef struct Tree Tree; // Let "Tree" mean the same as "struct Tree".

Tree *tree_new();

void tree_free(Tree *);

__attribute__((unused)) char *tree_list(Tree *tree, const char *path);

__attribute__((unused)) int tree_create(Tree *tree, const char *path);

__attribute__((unused)) int tree_remove(Tree *tree, const char *path);

__attribute__((unused)) int tree_move(Tree *tree, const char *source, const char *target);
