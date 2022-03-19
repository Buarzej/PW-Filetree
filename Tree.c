#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "Tree.h"
#include "HashMap.h"
#include "path_utils.h"
#include "readers_writers.h"

struct Tree {
    HashMap *directories;
    Readwrite *readwrite;
};

// Iterate over a path and get its node.
// Returns NULL if directory described by path doesn't exist.
// Setting 'as_writer' to true will make the iteration lock subsequent
// directories as writer instead of reader.
// The caller should later unlock returned directory (unless it doesn't exist).
Tree *get_directory_tree(Tree *root, const char *path, bool as_writer) {
    if (path == NULL)
        return NULL;

    Tree *curr_tree = root;
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    const char *subpath = path;

    if (!as_writer)
        rw_before_read(curr_tree->readwrite);
    else
        rw_before_write(curr_tree->readwrite);

    while ((subpath = split_path(subpath, component))) {
        Tree *next_tree = hmap_get(curr_tree->directories, component);
        if (next_tree == NULL) {
            if (!as_writer)
                rw_after_read(curr_tree->readwrite);
            else
                rw_after_write(curr_tree->readwrite);

            return NULL; // Directory doesn't exist.
        }

        if (!as_writer) {
            rw_before_read(next_tree->readwrite);
            rw_after_read(curr_tree->readwrite);
        } else {
            rw_before_write(next_tree->readwrite);
            rw_after_write(curr_tree->readwrite);
        }

        curr_tree = next_tree;
    }

    return curr_tree;
}

// Iterate over the whole subtree and lock each of its subdirectories as a 'remover'.
// This makes sure that all of the processes working in a subtree will have ended.
void lock_subtree(Tree *tree) {
    const char *key;
    void *value;
    HashMapIterator it = hmap_iterator(tree->directories);

    rw_before_remove(tree->readwrite);
    while (hmap_next(tree->directories, &it, &key, &value))
        lock_subtree((Tree *) value);
}

Tree *tree_new() {
    Tree *tree = malloc(sizeof(Tree));
    tree->directories = hmap_new();
    tree->readwrite = rw_new();

    return tree;
}

void tree_free(Tree *tree) {
    // Iterate over the map of directories and free all the subdirectories.
    const char *key;
    void *value;
    HashMapIterator it = hmap_iterator(tree->directories);
    while (hmap_next(tree->directories, &it, &key, &value))
        tree_free((Tree *) value);

    hmap_free(tree->directories);
    rw_free(tree->readwrite);

    free(tree);
}

__attribute__((unused)) char *tree_list(Tree *tree, const char *path) {
    if (!is_path_valid(path))
        return NULL;

    Tree *dir_tree = get_directory_tree(tree, path, false);
    if (dir_tree == NULL) {
        return NULL;
    } else {
        char *result = make_map_contents_string(dir_tree->directories);
        rw_after_read(dir_tree->readwrite);

        return result;
    }
}

__attribute__((unused)) int tree_create(Tree *tree, const char *path) {
    if (!is_path_valid(path))
        return EINVAL;
    else if (strcmp(path, "/") == 0)
        return EEXIST;

    Tree *parent_tree;
    char child[MAX_FOLDER_NAME_LENGTH + 1];
    char parent[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_parent = make_path_to_parent(path, child);

    // Lock parent directory as writer.
    if (strcmp(path_to_parent, "/") == 0) {
        free(path_to_parent);
        parent_tree = tree;
        rw_before_write(parent_tree->readwrite);
    } else {
        char *path_to_grandparent = make_path_to_parent(path_to_parent, parent);
        free(path_to_parent);

        Tree *grandparent_tree = get_directory_tree(tree, path_to_grandparent,
                                                    false);
        free(path_to_grandparent);

        if (grandparent_tree == NULL)
            return ENOENT;

        parent_tree = hmap_get(grandparent_tree->directories, parent);
        if (parent_tree == NULL) {
            rw_after_read(grandparent_tree->readwrite);
            return ENOENT;
        }

        rw_before_write(parent_tree->readwrite);
        rw_after_read(grandparent_tree->readwrite);
    }

    // Create new directory.
    Tree *new_tree = tree_new();
    if (!hmap_insert(parent_tree->directories, child, new_tree)) {
        rw_after_write(parent_tree->readwrite);
        tree_free(new_tree);
        return EEXIST;
    }

    rw_after_write(parent_tree->readwrite);

    return 0;
}

__attribute__((unused)) int tree_remove(Tree *tree, const char *path) {
    if (!is_path_valid(path))
        return EINVAL;
    else if (strcmp(path, "/") == 0)
        return EBUSY;

    Tree *parent_tree;
    char child[MAX_FOLDER_NAME_LENGTH + 1];
    char parent[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_parent = make_path_to_parent(path, child);

    // Lock parent directory as writer.
    if (strcmp(path_to_parent, "/") == 0) {
        free(path_to_parent);
        parent_tree = tree;
        rw_before_write(parent_tree->readwrite);
    } else {
        char *path_to_grandparent = make_path_to_parent(path_to_parent, parent);
        free(path_to_parent);

        Tree *grandparent_tree = get_directory_tree(tree, path_to_grandparent,
                                                    false);
        free(path_to_grandparent);

        if (grandparent_tree == NULL)
            return ENOENT;

        parent_tree = hmap_get(grandparent_tree->directories, parent);
        if (parent_tree == NULL) {
            rw_after_read(grandparent_tree->readwrite);
            return ENOENT;
        }

        rw_before_write(parent_tree->readwrite);
        rw_after_read(grandparent_tree->readwrite);
    }

    Tree *child_tree = hmap_get(parent_tree->directories, child);
    if (child_tree == NULL) {
        rw_after_write(parent_tree->readwrite);
        return ENOENT;
    }

    // Lock directory as remover.
    // This makes sure all of the processes working inside will have ended.
    rw_before_remove(child_tree->readwrite);

    if (hmap_size(child_tree->directories) != 0) {
        rw_after_write(parent_tree->readwrite);
        return ENOTEMPTY;
    }

    // Remove directory.
    tree_free(child_tree);
    hmap_remove(parent_tree->directories, child);

    rw_after_write(parent_tree->readwrite);

    return 0;
}

__attribute__((unused)) int tree_move(Tree *tree, const char *source, const char *target) {
    bool is_targets_parent_lca = false;
    bool is_sources_parent_lca = false;

    if (!is_path_valid(source) || !is_path_valid(target))
        return EINVAL;
    else if (strcmp(source, "/") == 0)
        return EBUSY;
    else if (strcmp(target, "/") == 0)
        return EEXIST;

    // In case of trying to move the folder to itself, do nothing.
    if (strcmp(source, target) == 0)
        return 0;

    // In case of trying to move the folder to its own subdirectory,
    // throw a -1 error code.
    if (is_subdirectory(source, target))
        return -1;

    // Lock LCA of source's and target's parents' as writer.
    char target_name[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_target_parent = make_path_to_parent(target, target_name);
    char source_name[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_source_parent = make_path_to_parent(source, source_name);

    Tree *lca_tree;
    char *lcp = longest_common_path(path_to_source_parent,
                                    path_to_target_parent);
    char lca[MAX_FOLDER_NAME_LENGTH + 1];
    char *path_to_lca_parent = make_path_to_parent(lcp, lca);

    if (path_to_lca_parent == NULL) {
        // LCP is root.
        lca_tree = tree;
        rw_before_write(lca_tree->readwrite);
    } else {
        Tree *lca_parent_tree = get_directory_tree(tree, path_to_lca_parent,
                                                   false);
        free(path_to_lca_parent);

        if (lca_parent_tree == NULL) {
            free(path_to_target_parent);
            free(path_to_source_parent);
            free(lcp);

            return ENOENT;
        }

        lca_tree = hmap_get(lca_parent_tree->directories, lca);
        if (lca_tree == NULL) {
            free(path_to_target_parent);
            free(path_to_source_parent);
            free(lcp);
            rw_after_read(lca_parent_tree->readwrite);

            return ENOENT;
        }

        rw_before_write(lca_tree->readwrite);
        rw_after_read(lca_parent_tree->readwrite);
    }

    // Lock target's parent as writer.
    Tree *target_parent_tree;
    const char *path_to_target_parent_from_lca = remove_prefix(lcp,
                                                               path_to_target_parent);

    char lca_child_heading_target[MAX_FOLDER_NAME_LENGTH + 1];
    const char *path_to_target_parent_from_lca_child = split_path(
            path_to_target_parent_from_lca, lca_child_heading_target);

    if (path_to_target_parent_from_lca_child == NULL) {
        // Target's parent is the same as LCA.
        is_targets_parent_lca = true;
        target_parent_tree = lca_tree;
    } else {
        Tree *lca_child_heading_target_tree = hmap_get(lca_tree->directories,
                                                       lca_child_heading_target);
        if (lca_child_heading_target_tree == NULL) {
            free(path_to_target_parent);
            free(path_to_source_parent);
            free(lcp);
            rw_after_write(lca_tree->readwrite);

            return ENOENT;
        }

        target_parent_tree = get_directory_tree(lca_child_heading_target_tree,
                                                path_to_target_parent_from_lca_child,
                                                true);
        if (target_parent_tree == NULL) {
            free(path_to_target_parent);
            free(path_to_source_parent);
            free(lcp);
            rw_after_write(lca_tree->readwrite);

            return ENOENT;
        }
    }

    free(path_to_target_parent);

    if (hmap_get(target_parent_tree->directories, target_name) != NULL) {
        free(path_to_source_parent);
        free(lcp);
        rw_after_write(lca_tree->readwrite);
        if (!is_targets_parent_lca)
            rw_after_write(target_parent_tree->readwrite);

        return EEXIST;
    }

    // Lock source's parent as writer.
    Tree *source_parent_tree;
    const char *path_to_source_parent_from_lca = remove_prefix(lcp,
                                                               path_to_source_parent);
    free(lcp);

    char lca_child_heading_source[MAX_FOLDER_NAME_LENGTH + 1];
    const char *path_to_source_parent_from_lca_child = split_path(
            path_to_source_parent_from_lca, lca_child_heading_source);

    if (path_to_source_parent_from_lca_child == NULL) {
        // Source's parent is the same as LCA.
        is_sources_parent_lca = true;
        source_parent_tree = lca_tree;
    } else {
        Tree *lca_child_heading_source_tree = hmap_get(lca_tree->directories,
                                                       lca_child_heading_source);
        if (lca_child_heading_source_tree == NULL) {
            free(path_to_source_parent);
            rw_after_write(lca_tree->readwrite);
            if (!is_targets_parent_lca)
                rw_after_write(target_parent_tree->readwrite);

            return ENOENT;
        }

        source_parent_tree = get_directory_tree(lca_child_heading_source_tree,
                                                path_to_source_parent_from_lca_child,
                                                true);
        if (source_parent_tree == NULL) {
            free(path_to_source_parent);
            rw_after_write(lca_tree->readwrite);
            if (!is_targets_parent_lca)
                rw_after_write(target_parent_tree->readwrite);

            return ENOENT;
        }
    }

    free(path_to_source_parent);

    Tree *source_tree = hmap_get(source_parent_tree->directories, source_name);
    if (source_tree == NULL) {
        rw_after_write(lca_tree->readwrite);
        if (!is_targets_parent_lca)
            rw_after_write(target_parent_tree->readwrite);
        if (!is_sources_parent_lca)
            rw_after_write(source_parent_tree->readwrite);

        return ENOENT;
    }

    // If possible, unlock LCA.
    if (!is_targets_parent_lca && !is_sources_parent_lca)
        rw_after_write(lca_tree->readwrite);

    // Lock enitre source subtree as remover.
    // This makes sure all of the processes working inside will have ended.
    lock_subtree(source_tree);

    // Move source to its new destination.
    Tree *new_node = malloc(sizeof(Tree));
    new_node->directories = source_tree->directories;
    new_node->readwrite = rw_new();

    hmap_insert(target_parent_tree->directories, target_name, new_node);
    rw_free(source_tree->readwrite);
    free(source_tree);
    hmap_remove(source_parent_tree->directories, source_name);

    if (is_targets_parent_lca && is_sources_parent_lca) {
        rw_after_write(lca_tree->readwrite);
    } else {
        rw_after_write(target_parent_tree->readwrite);
        rw_after_write(source_parent_tree->readwrite);
    }

    return 0;
}