/**
 * This file is copyright ©2009 Corvus Corax
 * Distributed under the terms of the GPL version 2 or higher
 */
/*This file contains the arglist class, to save the list of
  possible command line arguments*/

/*Maybe i should be proud of this, this is a classical!
  I always said "Who needs C++?"
  >;>>> */

struct arglist;
/*The structure sectorlist, public for pointers, members are private*/

struct arglist* arglist_new(int argc, char* argv[]);
/*A constructor recieves the command line arguments for initial processing
  returns NULL on failure
*/

int arglist_remarg (struct arglist* list,char* argument);
/*Removes an argument from the list
  0 for success, 1 for not found, -1 for error
*/

int arglist_kill(struct arglist* list);
/*A destructor, 0 for OK, -1 for error*/

int arglist_addarg (struct arglist* list,char* argument, int numparams);
/*Adds a possible argument to the list
  0: added, 1: changed, -1: failure
*/

int arglist_arggiven (struct arglist* list, char* argument );
/*Returns 0 if an argument has been specified, 1 if not, -1 on error*/

char* arglist_parameter (struct arglist* list, char* argument, int param);
/*Lists the specified Parameter of a given argument
  NULL if something wrong or not given
*/

int arglist_isinteger (char* text);
/*Returns 0 if char text can be translated into a number
  -1 of not
*/

int arglist_integer (char* text);
/*Returns numbertranslation of string text, 0 if not successful,
  do use arglist_isinteger first*/

