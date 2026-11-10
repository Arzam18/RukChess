.section .rodata
.balign 16
.global rukchess_embedded_nnue_start
.type rukchess_embedded_nnue_start, %object
rukchess_embedded_nnue_start:
.incbin "net-7342fb032855.nnue"
.global rukchess_embedded_nnue_end
rukchess_embedded_nnue_end:
.size rukchess_embedded_nnue_start, rukchess_embedded_nnue_end-rukchess_embedded_nnue_start
.section .note.GNU-stack,"",%progbits
