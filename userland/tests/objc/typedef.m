/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#import <libfoundation/NSObject.h>
#import <libobjc/helpers.h>
#import <stdio.h>

#define TRUE 1
#define FALSE 0

typedef struct Books {
   NSString *operatingsystem;
   NSString *author;
   NSString *language;
   int stars;
   int forks
} Book;
 
int main() {
   Book book;
   book.operatingsystem = @"pranaOS";
   book.author = @"Krisna Pranav";
   book.language = @"C / C++";
   book.stars = 37;
   book.forks = 21
   
   printf( "OPERATING SYSTEM : %@\n", book.operatingsystem);
   printf( "Author : %@\n", book.author);
   printf( "Language : %@\n", book.language);
   printf( "Stars : %d\n", book.stars);
   printf( "Forks : %d\n", book.forks);

   printf( @"Value of TRUE : %d\n", TRUE);
   printf( @"Value of FALSE : %d\n", FALSE);

   return 0;
}