#ifndef OPCODES_H
#define OPCODES_H

// Mapping hex values to instruction branches
// Trie-routing: 0x41 = 'A', 0x31 = '1' -> 'A1'
#define OP_MOVE    0x4131 
#define OP_SAY     0x5359
#define OP_IF      0x4946
#define OP_JMP     0x4A4D
#define OP_HALT    0x484C

#endif
