/***    morse - an automatic morse code teaching machine
/*
/*              Copyright (c) 1978 Howard Cunningham all rights reserved
/*              Copyright (c) 1988 Jim Wilson
/*
/*  Revision History: 04/18/78 initial pascal version adapted from 8080
/*                    12/04/81 corrected typo (div for mod in select)
/*                    06/18/88 translated to C
/*                    07/14/98 corrected accelerated character introduction
/*                               omitted in translation to C
/*
/*  Reference:  "A Fully Automatic Morse Code Teaching Machine",
/*              QST, (May 1977), ARRL, Newington CT
*/
#include <stdio.h>

/***    tweekable parameters and magic numbers */

#define WPM 15                  /* Initial codespeed in words-per-minute */
#define isdah(c) (c&1)          /* Mask l.s.b. to test dot or dash */
#define TONE_ON  1162           /* Period in microseconds (1Khz) */
#define TONE_OFF 0              /* Special value for silent period */
#define NLINE 25                /* Number of lines on PC screen and */
#define LINLEN 72               /*   number of characters on each line */
#define BARHT (NLINE-6)         /* Maximum height of error rate bars */
#define GOOD 0                  /* The best one can do */
#define BAD  255                /* The worst one can do */
#define DIT(wpm) (1395/(wpm))   /* Dit length for beep() - roughly in mS */
#define BRGRY 176               /* Bargraph character for inactive letters */
#define BRCHR 219               /* Bargraph character for active latters */

/***    display character attributes for various screen regions
 *
 *   These are 8-bit values left-justified into a 16-bit field (as
 *   required by pc(), prc() in beep.s).  For example, with a color-
 *   capable display in one of its text modes:
 *
 *              FrgbIRGB 00000000
 *
 *   where 'f' is set to flash (or blink) the character.
 *         'rgb' are set to specify the presense of red,
 *                 green and/or blue in the background.
 *         'I' is set to "intensify" the character
 *         'RGB' are set to specify the presense of red,
 *                 green and/or blue in the forground.
 */
#define NORM    0x1E00  /* Bargraph, herald:  Intensified Y/Blue */
#define HLGT    0x1F00  /* Active characters: intensified W/Blue */
#define GREY    0x1700  /* Inactive characters, W/Blue */
#define CWIN    0x0700  /* Code window, white on black */

/***    external functions from beep.s or the C runtime library */

extern srand(), rand();  /* seed, return random number (in C runtime) */
extern beep();           /* Sound tone for an interval (in beep.s)    */
extern unsigned ticks(); /* Return mS since last call  (in beep.s)    */
extern char resp();      /* nonblocking keyboard input (in beep.s)    */
extern unsigned tod();   /* DOS raw time-of-day counter (for srand()) */

unsigned num = 2;       /* Characters introduced so far (start with 2) */
unsigned dit = DIT(20); /* Default to 20 wpm */

/*** Letter[] holds character codes and error info for each letter
/*   MAXNUM is the largest size of the active alphabet.
/*   The 0th element is not part of the alphabet.  It is used to
/*     store an overall error indication for convenient display.
*/
#define MAXNUM (sizeof(letter)/sizeof(struct lt)-1) /* maximum alphabet */
#define OVERALL letter[0]                           /* for easy graphing!! */

struct lt {char ascii; char morse; unsigned char error;} letter[] = {
  {'*', 0,  GOOD},
  {'Q', 033, BAD}, {'7', 043, BAD}, {'Z', 023, BAD}, {'G', 013, BAD},
  {'0', 077, BAD}, {'9', 057, BAD}, {'8', 047, BAD}, {'O', 017, BAD},
  {'1', 076, BAD}, {'J', 036, BAD}, {'P', 026, BAD}, {'W', 016, BAD},
  {'L', 022, BAD}, {'R', 012, BAD}, {'A', 006, BAD}, {'M', 007, BAD},
  {'6', 041, BAD}, {'B', 021, BAD}, {'X', 031, BAD}, {'D', 011, BAD},
  {'Y', 035, BAD}, {'C', 025, BAD}, {'K', 015, BAD}, {'N', 005, BAD},
  {'2', 074, BAD}, {'3', 070, BAD}, {'F', 024, BAD}, {'U', 014, BAD},
  {'4', 060, BAD}, {'5', 040, BAD}, {'V', 030, BAD}, {'H', 020, BAD},
  {'S', 010, BAD}, {'I', 004, BAD}, {'T', 003, BAD}, {'E', 002, BAD},
};

/***    main - teach morse code
/*
/*  This program repeatedly selects a letter and teaches it to the
/*    student.  The student can request an evaluation and/or terminate
/*    the session.
*/
main(int argc, char *argv[]) {
  herald();                       /* Print program herald */
  srand(tod());                   /* seed random number generator */
  bgs();                          /* Show bargraph on screen */
  do  {
    pms(CWIN, "\013\n\n\n\n");    /* Clear code window */
    beep(TONE_OFF, 600);          /* Take a breath before we start */
    do ; while (teach(select())); /* teach letters */
  } while (menu());               /* grade and perhaps quit */
}

/***    herald - emit program proclamation
/*
/*  Herald() displays a copyright message and pauses long enough for
/*    the student to copy down an important URL (if she wants).
*/
herald() {
  pms(NORM, "\f\n\n\n\n\n\n\n\n"
	  "\230Morse Code Training Program\n"
    "\222(c) 1998 Ward Cunningham and Jim Wilson\n"
    "\222Permission granted to distribute freely\n"
	 "\227without profit or modification\n\n"
	 "\227See http://c2.com/~ward/morse/\n\n\n\n\n\n"
 "\217Try to type the character before the computer.\n"
	"\226Or, press Enter to take a break.");
  beep(TONE_OFF, 3000);
}

/***    weight - return weighted sum of two values
/*
/*  Weight() is passed two unsigned character parameters.
/*    It returns a weighted average of the two values.
/*
/*  Calling Sequence:  average = weight(v1, v2);
/*
/*     where (unsigned char) "average", "v1" and "v2" are related:
/*
/*            average = .875 * v1 + .125 * v2
*/
unsigned weight(unsigned v1, unsigned v2) {
  return ((7*v1 + v2 + 4) / 8);  /* "+ 4" forces rounding */
}

/***    teach - teach a morse letter
/*
/*  Teach() is passed an index to a letter.  It sends the letter
/*    in Morse on the PC's speaker and patiently waits for the
/*    student to press the corresponding key.  If too much time
/*    is taken, teach() gives a hint and resends the character
/*    until the student finally gets the answer right.  The student
/*    is graded on his performance.
/*
/*  Teach() returns a flag indicating that the student wants more.
/*
/*  Calling Sequence:   if (teach(lesson)) ...;
/*
/*    where (int) "lesson" is an index into the array letters[].
/*      "..." only if the student want's another character.
*/
teach(int lesson) {             /* letter[] index */
  register struct lt *l;        /* Pointer into letter[] */
  unsigned time;                /* time required for answer */
  unsigned score;               /* GOOD or BAD for this letter */
  int guess, answer;            /* student's answer, real solution */
  static int column = 0;        /* Characters remaining on line */
  static unsigned give = 3500;  /* Milliseconds to wait for answer */

  l = &letter[lesson];          /* Point register at lesson */
  answer = l->ascii;            /* Get solution to problem */
  score = GOOD;                 /* Assume the best */
  do  {
    if (column <= 0)  {         /* If at end-of-line */
      pc('\n'+CWIN);            /*   output newline and */
      column = LINLEN;          /*   rearm counter */
    }
    send(l->morse);             /* Send Morse character. */
    while (guess = resp())      /* Flush typeahead, but */
      if (guess == '\r')        /*   if student want's a break, */
	return column = 0;      /*     grant the wish */
    ticks(); time = 0;          /* Reset stopwatch. */
    do  {                       /* Loop to wait for answer: */
      guess = resp();           /*   Get student response (if any) */
      if (guess == answer)      /*   If correct answer, */
	break;                  /*     stop and update gradebook */
      if (guess == '\r')        /*   If student wants break, reset column */
	return column = 0;      /*     count (for next lesson), return */
      time += ticks();          /*   Add elapsed time */
    } while (time <= give);     /* Stop if student too slow */
    if (guess != answer)        /* If ever time out w.o. correct */
      score = BAD;              /*   answer, deduct some points */
    pc(answer+CWIN);            /* Echo correct answer */
    pc(' '+CWIN); column -= 2;  /*   and adorn it with a blank */
    give = weight(give, 2*time);/* Update response time */
    if (give>6000) give = 6000; /*   (but limit to 5-6 S.) */
    beep(TONE_OFF, 250);        /* Wait a brief interval */
  } while (guess != answer);
  grade(lesson, score);         /* Update gradebook */

  /* Student has answered correctly, so we'll give her some more.
   * Grade() has updated the overall and specific error estimates.
   * If the overall rate is low, and no specific character is too
   * bad, we will add another character to the training alphabet.
   */
  if (OVERALL.error > BAD * 3/10) return 1;
  for (l = &letter[num]; l > &OVERALL; l--)
    if (l->error > BAD * 4/10) return 1;
  add_ltr(); return 1;
}

/***    send - send charcter in morse code
/*
/*  Send() is passed a morse coded letter (see reference).
/*    It sends the character at speed "WPM" on the PC's speaker.
/*
/*  Calling Sequence:   send(morse);
/*
/*   where (char) "morse" is a stop-bit-prepended morse character.
*/
send(char code) {
  register element;
  do {
    element = dit;
    if (isdah(code)) element *= 3;
    beep(TONE_ON, element);
    beep(TONE_OFF, dit);
  } while ((code >>= 1) != 1);
}

/***    select - choose a letter from the current alphabet
/*
/*  Select() returns the index of a letter chosen from the
/*    currently active alphabet.  The probability of chosing
/*    a letter is proportional to the estimated error rate
/*    for that letter.
/*
/*  Calling Sequence:  new_letter = select();
/*
/*    where (int) "new_letter" is the index of the chosen element
/*      from letter[].
*/
select()
{ register int sum;     /* error probability accumulator */
  register int l;       /* working index into letter[] */

  sum = 0; l = 1;
  do  {
    sum += letter[l++].error + 1;
  } while (l <= num);
  sum = rand() % sum;
  do  {
    sum -= letter[--l].error + 1;
  } while (sum > 0);
  return (l);
}

/***    menu - display 4-line menu, one-line prompt, and get choice
/*
/*  Menu() displays a simple 4-line menu, and prompts for a selection.
/*    It awaits a valid choice and acts on it.  It returns nonzero or
/*    zero when "C(ontinue)" or "Q(uit)" are selected (respectively).
/*
/*  Calling Sequence:   int menu();
*/
menu() {
  pms(CWIN,     /* NOTE: Change showspd(), below, if you change menu!!! */
   "\nCharacter Code Speed:\203Practice Alphabet:\206Training:\n"
    "\202S(low --- 10 wpm)\207A(dd another letter)\203C(ontinue training)\n"
    "\202M(edium - 15 wpm)\207R(emove last letter)\203Q(uit program)\n"
    "\202F(ast --- 20 wpm)\nYour choice? (SMFARCQ): ");
  showspd();    /* Show codespeed (see NOTE, above) */
  for (;;) switch (resp()) {
    case 'S': dit = DIT(10); showspd(); break;
    case 'M': dit = DIT(15); showspd(); break;
    case 'F': dit = DIT(20); showspd(); break;
    case 'A': add_ltr();                break;
    case 'R': rem_ltr();                break;
    case 'C': return 1;
    case 'Q': return 0;
  }
}

/***    showspd - show current codespeed on menu
/*
/*  Showspd() erases column 19 on rows 21, 22, and 23 of the display,
/*  and then rewrites a '<' in one of the erased positions to show
/*  the code speed.
*/
showspd() {
  register int i;
  for (i = 21; i < 24; i++) prc(' '+CWIN, i, 19);
  switch (dit) {
    case DIT(10): i = 21; break;
    case DIT(15): i = 22; break;
    case DIT(20): i = 23; break;
  }
  prc('<'+CWIN, i, 19);
}

/***    pcs - put character and a space
/*
/*  Pcs() is passed a character to display (converted to an integer).  It
/*  displays the character (via putchar()) and then emits a trailing blank.
/*
/*  This dates back to the old BRB video terminal where displaying the
/*  characters with intervening blanks made the upper-case much more
/*  readable.
/*
/*  Calling Sequence:   pcs(int c);
*/
pcs(int c) { putchar(c); putchar(' '); }

/***    pms - put message to screen
/*
/*  Pms() is passed a integer character "attribute" (that controls the
/*    character color, blinking, etc.) and a string that can contain:
/*
/*    1. '\f' to clear the screen (using the attribute as fill) and
/*         move the cursor to the leftmost column in the first line.
/*    2. '\n' to advance the cursor to first column of the next line
/*         (scrolling if necessary).
/*    3. 0x80-flagged characters which represent sequences of blanks
/*         (after stripping the leftmost bit).
/*    4. Normal ASCII characters (<= 0x7F) which will be displayed
/*         on the screen.
/*
/*  Calling Sequence:   pms(int attribute, char *message);
*/
pms(int attr, char *msg) {
  register int c;
  register int i;
  while (c = *msg++) {  /* While more message left to display, */
    if (c & 0x80) {     /*   If compressed blank, */
      for (c &= 0x7F; c; c--)/* display a sequence of blanks */
	pc(attr+' ');
    }                   /*   If other character, */
    else pc(attr+c);    /*     Let the low-level code handle it */
  }
}

/***    barht - convert score (in [0,BAD]) to bar graph height
/*
/*  Barht() is passed an integer error rate in [0,BAD].  It linearly
/*    scales this value to [0,BARHT], the number of illuminated lines
/*    in a crude bar graph displayed in a column on the screen.
/*
/*  Calling Sequence:   int barht(int score);
*/
barht(int score) { return (score*BARHT + BAD/2)/BAD; }

/***    grade - update error estimates
/*
/*  Grade() is passed an index to the current letter and GOOD
/*    if the student got the right answer or BAD if she had to
/*    be told the answer.  Grade() updates the particular and
/*    overall error rate estimates and revises the displayed
/*    bargraph to display the change.
/*
/*  Calling sequence:   grade(ltr, grade);
/*
/*    where (int) "ltr" is the index of the current letter
/*       and (int) "grade" is GOOD or BAD.
*/
grade(unsigned ltr, unsigned g) {       /* Index to lesson, test results */
  register struct lt *l;                /* Pointer to element being dated */
  int old, new;                         /* Old, bar first display row */
  l = &letter[ltr];                     /* Aim register into letter[] */
  ltr = (ltr-1)*2;                      /* Ltr = column in bargraph */
  old = BARHT - barht(l->error);        /* 1st row that now has a bar */
  update(l, g);                         /* Update specific error rate */
  if (update(&OVERALL, g) < BAD * 1/10) /* If overall error rate low, */
    update(l, g);                       /*   accelerate specific rate */
  new = BARHT - barht(l->error);        /* 1st row that SHOULD have bar */
  while (new != old) {                  /* Until graph bar is right sized, */
    if (new > old)                      /*   If too tall, */
      prc(' '  +NORM, old++, ltr);      /*     chop it down to size */
    else                                /*   If too short, */
      prc(BRCHR+NORM, --old, ltr);      /*     build it up a bit */
  }
}

/***    update - update error rate
/*
/*  Update() is passed the address of an element of letter[], and a grade
/*    (either GOOD or BAD).  It updates the .error probability estimate
/*    field, and returns the new value for this field.
/*
/*  Calling sequence:   int update(struct lt *ltr, int grade);
*/
update(struct lt *l, unsigned g) { return l->error = weight(l->error, g); }

/***    bgs - display (initial) bargraph on screen
/*
/*  Bgs() erases the herald display (see herald(), above), and replaces
/*    it with a crude bargraph of the student's error rate (i.e. the
/*    letter[].error fields).  Below each bar, the corresponding letter
/*    (i.e. the letter[].ascii) is displayed.  Active letters are dis-
/*    tinguished by displaying their bargraph as a solid bar; inactive
/*    letters' bars are "greyed".
/*
/*  Calling Sequence: bgs();
*/
bgs() {
  register int i = 0;     /* Index into letter[] */
  pc('\f'+NORM);          /* Clear screen */
  do drwbar(++i);         /* Draw bargraph */
  while (i < MAXNUM);
}

/***    add_ltr, rem_ltr - add letter/remove letter from training aphabet
/*
/*  Add_ltr() checks "num", the number of letters in the training alphabet
/*  and increases it (assuming it is not maxed out already).  The appear-
/*  ance of the new character (its bargraph bar) is redrawn as solid to
/*  reflect its new status.
/*
/*  Rem_ltr() decreases the number of letters, (but not less than 1) and
/*  "greys" the bargraph appropriately.
/*
/*  Calling Sequence:   add_ltr(); ...  rem_ltr();
*/
add_ltr() { if (num < MAXNUM) drwbar(++num); } /* Room to add? redraw bar */
rem_ltr() { if (num > 1)      drwbar(num--); } /* More than 1? redraw bar */

/***    drwbar - draw bargraph bar
/*
/*  Drw_bar() is passed an index into letter[] (>= 1).  It draws a
/*    a "greyed" or "solid" (depending upon whether the corresponding
/*    letter is part of the current trainin alphabet) whose height is
/*    proportional to letter[].error.
/*
/*  Calling Sequence:   drwbar(int index);
*/
drwbar(int c) {                         /* Column (nee index) for display */
  register int r = letter[c].ascii;     /* Letter, later its bargraph row */
  int s = BARHT-barht(letter[c].error); /* Number of spaces ABOVE bar */
  int bc = BRCHR;                       /* Bargraph char or greyed version */

  if (c <= num) r += HLGT;      /* If part of training set, highlite on */
  else r += GREY, bc = BRGRY;   /* Otherwise, deemphasize and grey its bar */
  c = c-1 << 1;                 /* C = column for letter and its bar */
  prc(r, BARHT, c);             /* Display annotation */
  r = BARHT; do {               /* Loop to draw a bar (and space above) */
    if (r-- == s) bc = ' ';     /*   If time to switch from draw to erase */
    prc(bc+NORM, r, c);         /*   Put (char) at (screen) row, column */
  } while (r);                  /* Until reach top of screen */
}
