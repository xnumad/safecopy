/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
/*This file contains the arglist class, to save the list of
  possible command line arguments*/

/*Maybe i should be proud of this, this is a classical!
  I always said "Who needs C++?"
  >;>>> */

/*This file should really do all our command line checking work,
  not only save data in some list files :-) */

#include <stdlib.h>
#include <string.h>
#include "textlist.h"
/*We include the Text-list class*/
#include "voidlist.h"
/*We include the List object motherclass*/

/* Private structures --------------------------------------------------*/


/* Our Identification (List Index) Structure is a char*, so not given*/

struct argdata {
   int numparams;
   struct textlist *parameters;
};
/* Our List Data Structure. uses textlist, too*/



/* Public structures --------------------------------------------------*/

struct arglist {
   struct textlist* given_arguments;
   struct voidlist* arglist;
   int argc;
   char **argv;
};

/* The structure arglist. It contains both a voidlist (modified here)
   and an ordinary textlist */

/* Private functions --------------------------------------------------*/

int arglist_greater (struct voidident *vfrst, struct voidident *vscnd) {
   char *frst, *scnd;
   frst=(char*)vfrst;
   scnd=(char*)vscnd;
   return ( strcmp(frst,scnd)>0);
}

int arglist_equality (struct voidident *vfrst, struct voidident *vscnd) {
   char *frst, *scnd;
   frst=(char*)vfrst;
   scnd=(char*)vscnd;
   return ( !strcmp(frst,scnd) );
}

/* ------------------------------------------------------------------- */

int arglist_freecontent (struct voidident *videntity, struct voiddata *vdata){
   char *identity;
   struct argdata *data;
   identity=(char*)videntity;
   data=(struct argdata*)vdata;

   if (data->parameters!=NULL){
      textlist_kill(data->parameters);
   }
   data->parameters=NULL;
   /*We may have a list in the data structure, which has to be erased first*/
   free (data);
   return 0;
}

/* Function for comparisation of Index values, and to free the memory of
   the data of our list members. To tell the upper voidlist class.
   I think this is some kind of workaround since ident and data are no
   object classes but ordinary var structs */

/* ------------------------------------------------------------------- */

int arglist_addtoknown (int number,struct voidident *videntity, struct voiddata *vdata, void* userdata) {
   char *identity;
   struct argdata *data;
   struct textlist* check;

   identity=(char*)videntity;
   data=(struct argdata*)vdata;
   check=(struct textlist*)userdata;

   return textlist_addline(check,identity);
}
/* Return procedure to safe all arguments i know about temporarily*/

int arglist_isinteger (char* text);
/*We need this public function for a test, so do prototype
  u will find that function further down <g>*/

int process_args (struct arglist* list) {
/*We go through all of our arguments and try to guess what they mean*/

/*1st delete all parameters of all arguments. this will be O(N^much):-(*/

   struct argdata* data;
   int t,tt;
   char *temp,*temp2;
   struct textlist* check;

   check=textlist_new();
   if (!check) return -1;

   if (voidlist_all (list->arglist,arglist_addtoknown, (void*)check)==-1)
      return -1;
   /* We defined a function which added all known arguments via the print
      option and our small addtoknown callfunction into the known_parameters
      textlist */

   /*Now we proceed through every line of this and delete whats there*/
   t=0;
   temp=textlist_line(check,t++);
   while (temp!=NULL) {
      data=(struct argdata*)voidlist_item (	list->arglist,
   						(struct voidident*) temp,
   						arglist_greater,arglist_equality);
      if (data->parameters!=NULL) {
         textlist_kill(data->parameters);
	 data->parameters=NULL;
      }
      temp=textlist_line(check,t++);
   }
   /*Done. We have a virgin list again*/

   t=0;
   temp=textlist_line(list->given_arguments,t++);
   while (temp!=NULL) {
      data=(struct argdata*)voidlist_item (	list->arglist,
		   				(struct voidident*) temp,
   						arglist_greater,arglist_equality);
      if (data!=NULL) {
         /*Jippieh. We found an Argument, lets look if it has parameters*/
	 if (data->parameters!=NULL) {
	    /*We have the argument twice :-( kill previous instance*/
	    textlist_kill(data->parameters);
	 }
	 data->parameters=textlist_new();
	 if (!data->parameters) return -1;
         temp=textlist_line(list->given_arguments,t++);
	 tt=0;
	 while ((tt<data->numparams) && (temp!=NULL)) {
	    if (strncmp(temp,"-",1) || (arglist_isinteger(temp)==0)) {
	       /* It is really a parameter*/
	       if (textlist_addline (data->parameters,temp)==-1) return -1;
               temp=textlist_line(list->given_arguments,t++);
	       tt++;
	    } else {
	       /*It is another Argument!*/
	       tt=data->numparams;
	    }
	 }
      }else {
         /*We have an unknown argument*/
	 temp2="VOIDARGS";
         data=(struct argdata*) voidlist_item (	list->arglist,
		   				(struct voidident*) temp2,
   						arglist_greater,arglist_equality);
         if (data->parameters==NULL) {
	    /*The void parameter list was empty yet*/
	    data->parameters=textlist_new();
	    if (!data->parameters) return -1;
	 }
         if (textlist_addline (data->parameters,temp)==-1) return -1;
         /*Void parameter was added*/
	 temp=textlist_line(list->given_arguments,t++);
      }
   }


   textlist_kill(check);
   /*Dont need this anymore*/

   return 0;
}

/* ------------------------------------------------------------------- */

int parse_args (struct arglist* list,int argc, char* argv[]) {
/* We parse the argvs, to seperate arguments which have "=" and similar in them*/
int t,tt,len;
char* temp;
char A;

   t=1;
   while (t<argc) {
      temp=argv[t];
      len=strlen(temp);
      tt=0;
      if (!strncmp(temp,"-",1)) {
         /* We do only seperate on = and : if it is an option, and no parameter*/
         while (tt<len) {
            A=temp[tt];
	    if (A==' ' || A=='=' || A==':') {
	       temp[tt]='\0';
	       if (textlist_addline(list->given_arguments,temp)==-1) return -1;
	       temp=&temp[tt+1];
	       len=strlen(temp);
	       tt=-1;
	       /*String is split*/
	    }
	    tt++;
         }
      }
      if (len>0) {
         if (textlist_addline(list->given_arguments,temp)==-1) return -1;
      }
      /* Each option is added to the given_arguments textlist*/
      t++;
   }

   return 0;
}

/* Public functions ---------------------------------------------------*/

int arglist_addarg (struct arglist* list,char* argument, int numparams) {
/*Adds an argument to the list
  0: added, 1: changed, -1: failure
*/
   struct argdata *data;
   int retval;

   if (!list) return -1;

   data=(struct argdata*)malloc(sizeof(struct argdata));
   if (!data) return -1;
   /*We have to create a special structure in memory to add to our list */

   data->numparams=numparams;
   data->parameters=textlist_new();
   if (!data->parameters) {
      free(data);
      return -1;
   }
   /*And fill them with data*/


   retval=voidlist_additem (	list->arglist,
   				(struct voidident*)argument,
				(struct voiddata*)data,
   				arglist_greater,arglist_equality,
				arglist_freecontent);

   if (retval==-1) return -1;
   /*And include this data in the list structure*/
   if (process_args(list)==-1) return -1;
   /*Reprocess arguments and parameters*/
   return retval;
}

/* ------------------------------------------------------------------- */

struct arglist* arglist_new(int argc, char* argv[]) {
/*A constructor recieves the command line arguments for initial processing
  returns NULL on failure
*/
  
   struct arglist* newarglist;
   int t;

   newarglist=(struct arglist*)malloc(sizeof(struct arglist));
   if (!newarglist) return NULL;
   newarglist->arglist=voidlist_new();
   if (!newarglist->arglist) {
      free (newarglist);
      return NULL;
   }
   newarglist->given_arguments=textlist_new();
   if (!newarglist->given_arguments) {
      free (newarglist->arglist);
      free (newarglist);
      return NULL;
   }

   newarglist->argc=argc;
   newarglist->argv=(char **)malloc(argc*sizeof(char*));
   if (!newarglist->argv) {
      free (newarglist->given_arguments);
      free (newarglist->arglist);
      free (newarglist);
      return NULL;
   }
   t=0;
   while (t<argc) {
      newarglist->argv[t]=strdup(argv[t]);
      t++;
   }
   /*Now this is great. copy the argv[] structure to mess with it*/

   if (parse_args(newarglist,newarglist->argc,newarglist->argv)==-1)
      return NULL;
   /*parse them to split them up*/

   if (arglist_addarg(newarglist,"VOIDARGS",0)==-1) return NULL;
   return newarglist;
}

/* ------------------------------------------------------------------- */

int arglist_remarg (struct arglist* list,char* argument) {
/*Removes an argument from the list
  0 for success, 1 for not found, -1 for error
*/

   int retval;

   if (!list) return -1;

   retval=voidlist_remitem(	list->arglist,
   				(struct voidident*)argument,
   				arglist_greater,arglist_equality,
				arglist_freecontent);

   if (process_args(list)==-1) return -1;
   /*Reprocess arguments and parameters*/

   return retval;;
}

/* ------------------------------------------------------------------- */

int arglist_kill(struct arglist* list) {
/*A destructor, 0 for OK, -1 for error*/
   int t;

   if (!list) return -1;

   textlist_kill(list->given_arguments);
   /*The given arguments remean zombie in memory till now*/
   /*But at least their list structure is gone*/
   voidlist_kill(list->arglist,arglist_freecontent);

   t=0;
   while (t<list->argc) {
      free(list->argv[t]);
      t++;
   }
   /*We kill our copy of the argv structure first*/

   free(list);
   return 0;
}

/* ------------------------------------------------------------------- */

int arglist_arggiven (struct arglist* list, char* argument ) {
/*Returns 0 if an argument has been specified, 1 if not, -1 on error*/
   struct argdata* data;

   if (!list) return -1;

   data=(struct argdata*)voidlist_item (list->arglist,
   					(struct voidident*)argument,
   					arglist_greater,arglist_equality);
   if (data==NULL) return 1;
   if (data->parameters==NULL) return 1;

   return 0;

}

/* ------------------------------------------------------------------- */

char* arglist_parameter (struct arglist* list, char* argument, int param){
/*Lists the specified Parameter of a given argument
  NULL if something wrong or not given
*/

   struct argdata* data;

   if (!list) return NULL;

   data=(struct argdata*)voidlist_item (list->arglist,
		   			(struct voidident*)argument,
   					arglist_greater,arglist_equality);
   if (data==NULL) return NULL;
   if (data->parameters==NULL) return NULL;

   return textlist_line(data->parameters,param);
}

/* ------------------------------------------------------------------- */

int arglist_isinteger (char* text){
/*Returns 0 if char text can be translated into a number
  -1 if not
*/

   int t,len,result,isnegative;


   if (text==NULL) return -1;
   t=0;
   isnegative=0;
   len=strlen(text);
   result=0;
   if (len==0) return 0;
   while (t<len) {
      result*=10;
      if ((text[t]<'0') || (text[t]>'9')) {
         if (text[t]=='-') {
	    if (result==0) {
	       isnegative=1;
	    } else {
	       return -1;
	    }
	 } else if (text[t]==' ') {
	    result/=10;
	 } else {
	    return -1;
	 }
      } else {
         result+=1;
      }
      t++;
   }
   return 0;
}

/* ------------------------------------------------------------------- */

int arglist_integer (char* text){
/*Returns numbertranslation of string text, 0 if not successful,
  do use arglist_isinteger first*/
   int t,len,result,isnegative;


   if (text==NULL) return 0;
   t=0;
   isnegative=0;
   len=strlen(text);
   result=0;
   if (len==0) return 0;
   while (t<len) {
      result*=10;
      if ((text[t]<'0') || (text[t]>'9')) {
         if (text[t]=='-') {
	    if (result==0) {
	       isnegative=1;
	    } else {
	       return 0;
	    }
	 } else if (text[t]==' ') {
	    result/=10;
	 } else {
	    return 0;
	 }
      } else {
         if (text[t]=='1') result+=1;
         if (text[t]=='2') result+=2;
         if (text[t]=='3') result+=3;
         if (text[t]=='4') result+=4;
         if (text[t]=='5') result+=5;
         if (text[t]=='6') result+=6;
         if (text[t]=='7') result+=7;
         if (text[t]=='8') result+=8;
         if (text[t]=='9') result+=9;
      }
      t++;
   }
   if (isnegative==1) result=0-result;
   return result;

}

/* -End--------------------------------------------------------------- */
