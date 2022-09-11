/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
/*This file contains the textlist class, to save the list of
  simple char texts, instead of using arrays*/

/*Maybe i should be proud of this, this is a classical!
  I always said "Who needs C++?"
  >;>>> */

#include <stdlib.h>
#include "voidlist.h"
/*We include the List object motherclass*/

/* Private structures --------------------------------------------------*/


struct lineindex {
   int line;
};
/* Our Identification (List Index) Structure */

/* Our List Data Structure is char* directly <g>*/


/* Public structures --------------------------------------------------*/

struct textlist{
   struct voidlist *liste;
};

/*The structure textlist, public itself, but members are private*/
/*Since my new class system the members are taken from class voidlist,
  so even unknown to this very file ;) */

/* Private functions --------------------------------------------------*/

int textlist_greater (struct voidident *vfrst, struct voidident *vscnd) {
   struct lineindex *frst,*scnd;
   frst=(struct lineindex*)vfrst;
   scnd=(struct lineindex*)vscnd;
   return ( (frst->line>scnd->line) );
}

int textlist_equality (struct voidident *vfrst, struct voidident *vscnd) {
   struct lineindex *frst,*scnd;
   frst=(struct lineindex*)vfrst;
   scnd=(struct lineindex*)vscnd;
   return ( (frst->line==scnd->line) );
}

int textlist_freecontent (struct voidident *videntity, struct voiddata *vdata){
   struct lineindex *identity;
   identity=(struct lineindex*)videntity;
   free (identity);
   return 0;
}

/* Function for comparisation of Index values, and to free the memory of
   the data of our list members. To tell the upper voidlist class.
   I think this is some kind of workaround since ident and data are no
   object classes but ordinary var structs */


/* Public functions ---------------------------------------------------*/

struct textlist* textlist_new() {
/*A constructor*/
   struct textlist* newtext;

   newtext=(struct textlist*)malloc(sizeof(struct textlist));
   if (!newtext) return NULL;
   newtext->liste=voidlist_new();
   if (!newtext->liste) {
      free(newtext);
      return NULL;
   }


   return newtext;
}

/* ------------------------------------------------------------------- */

int textlist_remline (struct textlist* list,int line) {
/*Removes a textline from the list 0 on success, 1 on not found, -1 on error*/
   struct lineindex identity;

   if (!list) return -1;
   identity.line=line;
   return voidlist_remitem(	list->liste,
   				(struct voidident*)&identity,
   				textlist_greater,textlist_equality,
				textlist_freecontent);

}

/* ------------------------------------------------------------------- */

int textlist_kill(struct textlist* list) {
/*A destructor, 0 on success, -1 on error*/
   if (!list) return -1;
   voidlist_kill(list->liste,textlist_freecontent);
   free(list);
   return 0;
}

/* ------------------------------------------------------------------- */

int textlist_setline (struct textlist* list,int line, char* text) {
/*Sets a line on the list to specified value
  0 added, 1, changed, -1 error
*/
   struct lineindex *identity;

   if (!list) return -1;

   identity=(struct lineindex*)malloc(sizeof(struct lineindex));
   if (!identity) return -1;
   /*We have to create a special structure in memory to add to our list */

   identity->line=line;
   /*And fill them with data*/

   return voidlist_additem (	list->liste,
   				(struct voidident*)identity,
   				(struct voiddata*)text,
   				textlist_greater,textlist_equality,
				textlist_freecontent);
   /*And include this data in the list structure*/

}
/* ------------------------------------------------------------------- */

int textlist_addline (struct textlist* list,char* text){
/*Adds a line of text to the end of the list
  0 success, -1 error
*/
   struct lineindex *identity;
   int line;

   if (!list) return -1;

   identity=(struct lineindex*)voidlist_last(list->liste);
   if (!identity) {
      line=0;
   } else {
      line=identity->line+1;
   }
   /* We calculate the position which should be the last, but maybe is also the 1st */
   identity=(struct lineindex*)malloc(sizeof(struct lineindex));
   if (!identity) return -1;
   /*We have to create a special structure in memory to add to our list */

   identity->line=line;
   /*And fill them with data*/

   return voidlist_additem (	list->liste,
   				(struct voidident*)identity,
   				(struct voiddata*)text,
   				textlist_greater,textlist_equality,
				textlist_freecontent);
   /*And include this data in the list structure*/

}

/* ------------------------------------------------------------------- */

char* textlist_line (struct textlist* list, int line){
/*Gives back a single line from position line, NULL if N/A or error*/
   struct lineindex identity;
   if (!list) return NULL;
   identity.line=line;
   return ((char*)voidlist_item (	list->liste,
   					(struct voidident*)&identity,
   					textlist_greater,textlist_equality));
}

/* -End--------------------------------------------------------------- */
