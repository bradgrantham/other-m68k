# The editor

The editor is the program for writing and changing text files: scripts, BASIC programs,
notes, or anything else made of plain characters. It fills the screen with the file and
lets you move around in it and change it wherever you like.

If you have used a word processor or typed into a web page, most of the keys will already
be familiar.

## Opening a file

Type `edit` and the name of the file.

```
edit D:GREET.CMD
```

If the file exists, the editor shows it. If it does not, the editor starts with an empty
screen, and the file is created the first time you save.

![The editor, with one line selected.](shots/ed_open.png){width=62%}

The yellow block is the cursor. The bottom line of the screen is the **status line**. On
the left it shows the name of the file, and a star beside the name once you have changed
something and not yet saved it. On the right it shows where the cursor is, as a line
number and a column.

The monitor's screen is not lost while you edit. The editor draws on the second page of
the picture, so when you leave, everything you had on the screen before is still there.

## Typing and moving

What you type goes in at the cursor, and the text after it moves along to make room.
[Enter]{.key} starts a new line. [Backspace]{.key} rubs out the character before the
cursor and [Delete]{.key} the one under it.

The cursor keys move one step at a time. These keys move further:

| Key | Where the cursor goes |
|----------|--------------------|
| [Home]{.key} [End]{.key} | The start or the end of the line |
| [Ctrl]{.key} + [←]{.key} or [→]{.key} | One word left or right |
| [PgUp]{.key} [PgDn]{.key} | Up or down by a screen |
| [Ctrl]{.key} + [Home]{.key} | The top of the file |
| [Ctrl]{.key} + [End]{.key} | The bottom of the file |
| [Ctrl]{.key} + [G]{.key} | A line you name by number |

A line can be longer than the screen is wide. When the cursor reaches the right-hand
edge, the whole screen slides sideways to follow it, and slides back when you return.

## Selecting, cutting and pasting

Hold [Shift]{.key} and press any of the movement keys, and the text the cursor passes over
is **selected**. It shows in a different colour, as the third line does in the picture
above. Once some text is selected you can do several things with it:

| Key | What it does |
|----------|--------------------|
| [Ctrl]{.key} + [C]{.key} | Copies the selected text, leaving it where it is |
| [Ctrl]{.key} + [X]{.key} | Cuts it: copies it and removes it |
| [Ctrl]{.key} + [V]{.key} | Pastes the last thing copied or cut, at the cursor |
| [Backspace]{.key} or [Delete]{.key} | Removes it |
| Any character | Replaces it with what you type |
| [Ctrl]{.key} + [A]{.key} | Selects the whole file |

To move some lines, select them, cut them, move the cursor to their new place and paste.
You can paste the same text as many times as you like.

## Undoing a mistake

[Ctrl]{.key} + [Z]{.key} takes back the last change. Press it again to take back the one
before that, and so on back through your work. A run of typing counts as one change, so
one press removes the whole word or sentence you just typed and not a single letter.

Undo is what makes it safe to experiment. If you select the whole file and type a letter
by accident, the file is gone from the screen, and one press of [Ctrl]{.key} +
[Z]{.key} brings it back.

## Finding text

Press [Ctrl]{.key} + [F]{.key}. The status line asks what to find. Type a word or part of
one and press [Enter]{.key}.

![Asking the editor to find a word.](shots/ed_find.png){width=62%}

The editor looks forward from the cursor and selects the first place it finds. Capital and
small letters are treated as the same. [F3]{.key} finds the next place, and when the
search reaches the bottom of the file it carries on from the top.

Because the match is selected, you can type straight over it. Finding a word, typing its
replacement and pressing [F3]{.key} for the next one is a quick way to change a name
throughout a file.

[ESC]{.key} leaves the question without searching.

## Saving and leaving

[Ctrl]{.key} + [S]{.key} saves the file. The star on the status line goes out, and you can
carry on working.

[Ctrl]{.key} + [Q]{.key} leaves the editor. If there are changes you have not saved, it
asks first. Answer `y` to save them and leave, `n` to leave without saving, or press
[ESC]{.key} to go back to the file.

Save often. The text you are working on lives in RAM until you save it, and RAM forgets
everything if the power goes off.

::: try
Make the Griffin Jr. ask for the date and time whenever it starts. Type
`edit C:AUTOEXEC.CMD`, press [Ctrl]{.key} + [End]{.key} to go to the bottom, type `date`
and press [Enter]{.key}. Save with [Ctrl]{.key} + [S]{.key} and leave with [Ctrl]{.key} +
[Q]{.key}. Then press RESET.

If you change your mind, edit the file again and take the line out. And remember that
holding [ESC]{.key} during the memory test skips `AUTOEXEC.CMD` altogether.
:::

```{.screen session=ed_auto}
```

## All the keys

| Key | What it does |
|----------|--------------------|
| [←]{.key} [→]{.key} [↑]{.key} [↓]{.key} | Move the cursor |
| [Home]{.key} [End]{.key} | Start or end of the line |
| [Ctrl]{.key} + [←]{.key} [→]{.key} | One word left or right |
| [PgUp]{.key} [PgDn]{.key} | Up or down a screen |
| [Ctrl]{.key} + [Home]{.key} [End]{.key} | Top or bottom of the file |
| [Shift]{.key} + any of the above | Move and select |
| [Ctrl]{.key} + [A]{.key} | Select everything |
| [Ctrl]{.key} + [C]{.key} [X]{.key} [V]{.key} | Copy, cut, paste |
| [Ctrl]{.key} + [Z]{.key} | Undo |
| [Ctrl]{.key} + [F]{.key} | Find |
| [F3]{.key} | Find the next one |
| [Ctrl]{.key} + [G]{.key} | Go to a line by number |
| [Tab]{.key} | Spaces up to the next fourth column |
| [Ctrl]{.key} + [S]{.key} | Save |
| [Ctrl]{.key} + [Q]{.key} | Quit |

::: curious
**Things worth knowing**

The whole file is held in memory while you edit it, so the largest file you can open is
set by the free RAM: several hundred KB, which is a very long piece of writing.

Text files from different computers mark the end of each line in two different ways. The
editor reads both, and when it saves a file it uses the way the file already had. A file
you bring from another computer goes back unchanged apart from your edits.

Undo remembers about 32 KB of changes. If you make a single change larger than that, such
as cutting a very large block, that change and those before it can no longer be undone.

Saving to drive C: writes to the flash chips, which holds everything up for a moment and
wears them very slightly. For a file you are saving every few minutes all afternoon, the
SD card is the better home.
:::
