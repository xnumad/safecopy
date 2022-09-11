/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
/*This file contains a void list class, a generic list class
  with standard functions*/

/*Maybe i should be proud of this, this is a classical!
  I always said "Who needs C++?"
  >;>>> */

struct voidlist;
/*The structure voidlist, public for pointers, members are private*/

struct voidident;
/*The internal identificator to save in the list*/

struct voiddata;
/*The internal data to save in the list*/

struct voidlist* voidlist_new();
/*A constructor returns NULL on error*/

int voidlist_remitem (	struct voidlist* list,
			struct voidident* item,
			int (*greater)(struct voidident *frst, struct voidident *scnd),
			int (*equality)(struct voidident *frst, struct voidident *scnd),
			int (*freecontent)(struct voidident *identity,struct voiddata *data));
/*Removes an entry from the list*/
/*0 for found, 1 for not found -1 for error*/

int voidlist_kill(	struct voidlist* list,
			int (*freecontent)(struct voidident* identity,struct voiddata* data));
/*A destructori, 0 on success, -1 on error*/

int voidlist_additem (	struct voidlist* list,
			struct voidident* item,
			struct voiddata* data,
			int (*greater)(struct voidident *frst, struct voidident *scnd),
			int (*equality)(struct voidident *frst, struct voidident *scnd),
			int (*freecontent)(struct voidident *identity, struct voiddata *data));
/*Adds an item to the list*/
/*Returns 0 for success and 1 for data change, -1 for error*/

struct voiddata* voidlist_item (struct voidlist* list,
				struct voidident* item,
				int (*greater)(struct voidident *frst, struct voidident *scnd),
				int (*equality)(struct voidident *frst, struct voidident *scnd));
/*Gives out the data of the specified entry, NULL for error and not found*/

int voidlist_all(	struct voidlist* list,
			int (*listprint)(int number, struct voidident *identity, struct voiddata *data, void *userdata ),
			void *uderdata );
/*Gives out all the data via listprint*/
/*Gives back 1 when list is empty and 0 if not */
/*-1 on error*/

int voidlist_members (	struct voidlist* list);
/*Gives back the amount of List members*/

struct voidident* voidlist_first (struct voidlist* list);
/*Gives back the !identifier! (not the data) of the first item*/

struct voidident*  voidlist_last (struct voidlist* list);
/*Gives back the !identifier! (not the data) of the last item*/
