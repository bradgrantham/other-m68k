/* Generated from spec/board.json by tools/gen.py -- DO NOT EDIT; edit the spec and run `make gen`. */
MEMORY
{
    USER (rwx) : ORIGIN = 0x008000, LENGTH = 0x0D2800
}
_user_stack_top = ORIGIN(USER) + LENGTH(USER);
