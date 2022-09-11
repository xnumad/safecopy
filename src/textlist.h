/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
/*This file contains the textlist class, to save the list of
  simple char texts, instead of using arrays*/

/*Maybe i should be proud of this, this is a classical!
  I always said "Who needs C++?"
  >;>>> */

struct textlist;
/*The structure textlist, public for pointers, members are private*/

struct textlist* textlist_new();
/*A constructor NULL on error*/

int textlist_remline (struct textlist* list,int line);
/*Removes a textline from the list 0 on success, 1 on not found, -1 on error*/

int textlist_kill(struct textlist* list);
/*A destructor, 0 on success, -1 on error*/

int textlist_setline (struct textlist* list,int line,char* text);
/*Sets a line on the list to specified value
  0 added, 1, changed, -1 error
*/

int textlist_addline (struct textlist* list,char* text);
/*Adds a line of text to the end of the list
  0 success, -1 error
*/

char* textlist_line (struct textlist* list, int line);
/*Gives back a single line from position line, NULL if N/A or error*/

