/*
 * Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "flang/ADT/hash.h"
#include "flang/ArgParser/debug_action.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/** \brief Internal representation of the action map */
struct action_map_ {
  /** Hash map from string keys to executable actions */
  hashmap_t actions;
};

/** \brief An action is just a void function */
typedef void (*action_t)(void);

/** \brief Action list */
typedef struct action_list_ {
  /** A function pointer array */
  action_t *actions;
  /** Number of actions in the list */
  size_t count;
} action_list_t;

/** Allocate and initialize action map data structure */
void
create_action_map(action_map_t **map)
{
  *map = malloc(sizeof(action_map_t));
  (*map)->actions = hashmap_alloc(hash_functions_strings);
}

/** Deallocate action map data structure */
void
destroy_action_map(action_map_t **map)
{
  /* Free flags data structure */
  hashmap_free((*map)->actions);

  /* Deallocate the data structure itself */
  free(*map);
  *map = NULL;
}

/** Add action to action map */
void
add_action(action_map_t *map, const char *keyword, void (*action)(void))
{
  action_list_t *action_list = NULL;

  /* Check if there is something in the map already */
  if (hashmap_lookup(map->actions, keyword, (hash_data_t *)&action_list)) {
    /* Add one more element at the end of the list */
    ++action_list->count;
    action_list->actions =
        realloc(action_list->actions, sizeof(action_t) * action_list->count);
    action_list->actions[action_list->count - 1] = action;
  } else {
    /* Create brand new action list */
    action_list = (action_list_t *)malloc(sizeof(action_list_t));
    action_list->count = 1;
    action_list->actions = malloc(sizeof(action_t));
    *(action_list->actions) = action;
    /* Add it to the list */
    hashmap_insert(map->actions, keyword, action_list);
  }
}

/** Execute action(s) for a given keyword */
void
execute_actions_for_keyword(action_map_t *map, const char *keyword)
{
  action_list_t *action_list = NULL;

  /* Execute if there is anything for this keyword */
  if (hashmap_lookup(map->actions, keyword, (hash_data_t *)&action_list)) {
    size_t index;
    for (index = 0; index < action_list->count; ++index) {
      action_list->actions[index]();
    }
  }
  /* Do nothing is there is no record */
}

/** Copy an action from one map to another */
void
copy_action(const action_map_t *from, const char *keyword_from,
            action_map_t *to, const char *keyword_to)
{
  action_list_t *source_actions = NULL;
  action_list_t *dest_actions = NULL;

  /* Silently exit if nothing is found in source dataset */
  if (!hashmap_lookup(from->actions, keyword_from,
                      (hash_data_t *)&source_actions)) {
    return;
  }

  size_t count = source_actions->count;

  /* Check if there is something in the destination map already */
  if (hashmap_lookup(to->actions, keyword_to, (hash_data_t *)&dest_actions)) {
    /* Copy the array of function pointers */
    dest_actions->count = count;
    dest_actions->actions =
        realloc(dest_actions->actions, sizeof(action_t) * count);
    memcpy(dest_actions->actions, source_actions->actions,
           sizeof(action_t) * count);
  } else {
    /* Create new record identical to the old one */
    dest_actions = malloc(sizeof(action_map_t));
    dest_actions->count = count;
    dest_actions->actions = malloc(sizeof(action_t) * count);
    memcpy(dest_actions->actions, source_actions->actions,
           sizeof(action_t) * count);
    /* Add it to the list */
    hashmap_insert(to->actions, keyword_to, dest_actions);
  }
}
