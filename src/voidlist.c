/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
/*This file contains a void list class, a generic list class
  with standard functions*/

/*Maybe i should be proud of this, this is a classical!
  I always said "Who needs C++?"
  >;>>> */

#include <stdlib.h>
/* Private structures --------------------------------------------------*/

struct voidident;
struct voiddata;

struct voidentry {
   struct voidident *identity;
   struct voiddata *data;
   struct voidentry *next,*prev;
};

/*The structure sector to save a single entry*/

/* Public structures --------------------------------------------------*/

struct voidlist {
   struct voidentry *first,*last;
   int listcount;
};

/*The structure voidlist, public itself, but members are private*/

/* Public functions ---------------------------------------------------*/

struct voidlist* voidlist_new() {
/*A constructor*/
   struct voidlist* newlst;

   newlst=(struct voidlist*)malloc(sizeof(struct voidlist));
   if (!newlst) return NULL;

   newlst->first=NULL;
   newlst->last=NULL;
   newlst->listcount=0;
   return newlst;
}

/* ------------------------------------------------------------------- */

int voidlist_remitem (	struct voidlist* list,
			struct voidident* item,
			int (*greater)(struct voidident *frst, struct voidident *scnd),
			int (*equality)(struct voidident *frst, struct voidident *scnd),
			int (*freecontent)(struct voidident *identity,struct voiddata *data)) {
/*Removes an entry from the list*/
/*0 for found, 1 for not found, -1 for error */

   struct voidentry *member;

   if (!list) return -1;

   member=list->first;
   while (member!=NULL) {
      if (greater(member->identity,item)) {
         return 1;
         /*Requested member was not found, ergo est nada niente never noupe*/
      } else {
         if (equality(member->identity,item)) {
            /*We have a candidate*/
	    if (member->prev==NULL) {
	       /*And its the first in the list*/
	       list->first=member->next;
	    } else {
	       /*Or not <g>*/
	       member->prev->next=member->next;
	    }
	    if (member->next==NULL) {
	       /*But its the last in the list*/
	       list->last=member->prev;
	    } else {
	       /*Or not <g>*/
	       member->next->prev=member->prev;
	    }
	    list->listcount--;
	    /*Ok member has been remobed from list, so delete it*/
	    freecontent(member->identity,member->data);
	    free(member);
	    return 0;
	    /*Since we have a sorted list, this entry is sure to have
	      existed only once --- or has it ? happy debugging <g>*/
         }
	 member=member->next;
      }
   }
   /*Requested member was not found, ergo est nada niente never noupe*/
   return 1;

}

/* ------------------------------------------------------------------- */

int voidlist_kill(	struct voidlist* list,
			int (*freecontent)(struct voidident* identity,struct voiddata* data)){
/*A destructor, 0 on success, -1 on error*/

   struct voidentry *member;

   if (!list) return -1;

   while (list->first!=NULL) {
      member=list->first;
      list->first=member->next;
      freecontent(member->identity,member->data);
      free(member);
   }
   /* I now kill those entrys manually, because voidlist remitem needs far too much
      extra functions to run. too much overhead. i dont need comparisation for
      annihilation. who cares about colateral damage if you want to leave ashes*/
   /*I explicitly destroy list consistency here by the way, so there is no return
     when started. */

   free(list);
   return 0;
}

/* ------------------------------------------------------------------- */

int voidlist_additem (	struct voidlist* list,
			struct voidident* item,
			struct voiddata* data,
			int (*greater)(struct voidident *frst, struct voidident *scnd),
			int (*equality)(struct voidident *frst, struct voidident *scnd),
			int (*freecontent)(struct voidident *identity, struct voiddata *data)) {
/*Adds an entry to the list*/
/*Returns 0 for success and 1 for data change, -1 for error*/

   struct voidentry *member,*newmember;

   if (!list) return -1;
   
   member=list->first;
   while (member!=NULL) {
      /*We have entrys in the list*/
      if (greater(member->identity,item)) {
         /*We are behind the position where this should be*/
	 newmember=(struct voidentry*) malloc (sizeof(struct voidentry));
	 newmember->next=member;
	 newmember->prev=member->prev;
	 newmember->identity=item;
	 newmember->data=data;
	 /*New entry created*/
	 if (member->prev==NULL) {
	    /*ah yes. there is a list, but we just insert at the beginning*/
	    list->first=newmember;
	 } else {
	    /*we are just somewhere between, so do standard insert*/
	    member->prev->next=newmember;
	 }
	 member->prev=newmember;
	 list->listcount++;
	 /*And inserted*/
	 return 0;
      } else {
         /*We dont have the position yet*/
	 if (equality(item,member->identity)) {
	    /*Correct me, we are exatcly on the right position which meens
	      already there*/
	    freecontent(member->identity,member->data);
	    member->identity=item;
	    member->data=data;
	    return 1;
	 }
	 member=member->next;
      }
   }
   /*Ok we are at the very end*/
   newmember=(struct voidentry*) malloc (sizeof(struct voidentry));
   newmember->next=NULL;
   newmember->prev=list->last;
   newmember->identity=item;
   newmember->data=data;
   /*Create the new entry*/
   list->last=newmember;
   if (newmember->prev==NULL) {
      list->first=newmember;
   } else {
      newmember->prev->next=newmember;
   }
   list->listcount++;
   /*Put it into the list*/
   return 0;


}

/* ------------------------------------------------------------------- */

struct voiddata* voidlist_item (struct voidlist* list,
				struct voidident* item,
				int (*greater)(struct voidident *frst, struct voidident *scnd),
				int (*equality)(struct voidident *frst, struct voidident *scnd)){
/*Gives out the data of one entry, NULL for error and not found*/
   struct voidentry* member;

   if (!list) return NULL;

   member=list->first;
   while (member!=NULL) {
      if (greater(member->identity,item)) {
         return NULL;
         /*Requested member was not found, ergo est nada niente never noupe*/
      } else {
	 if (equality(member->identity,item)) {
	    return member->data;
	    /*We found it!*/
	 }
	 member=member->next;
      }
   }
   return NULL;
}
/* ------------------------------------------------------------------- */

int voidlist_all (	struct voidlist* list,
			int (*listprint)(int number, struct voidident *identity, struct voiddata *data, void *userdata ),
			void *userdata
			){
/*Gives out all the data via listprint*/
/*Gives back 1 when list is empty and 0 if not */
/*-1 on error*/
   int t=0;
   struct voidentry *member;

   if (!list) return -1;

   member=list->first;
   while (member!=NULL) {
      listprint (t++,member->identity,member->data,userdata);
      member=member->next;
   }

   if (t>0) {
      return 0;
   } else {
      return 1;
   }
}

/* ------------------------------------------------------------------- */

int voidlist_members (	struct voidlist* list){
/*Gives back the amount of List members*/

   if (!list) return 0;
   return list->listcount;
}

/* ------------------------------------------------------------------- */

struct voidident* voidlist_first (struct voidlist* list){
/*Gives back the !identifier! (not the data) of the first item*/

   if (!list) return NULL;

   if (list->first!=NULL) {
      return list->first->identity;
   } else {
      return NULL;
   }
}
/* ------------------------------------------------------------------- */

struct voidident* voidlist_last (struct voidlist* list){
/*Gives back the !identifier! (not the data) of the last item*/

   if (!list) return NULL;

   if (list->last!=NULL) {
      return list->last->identity;
   } else {
      return NULL;
   }
}
/* -End--------------------------------------------------------------- */
