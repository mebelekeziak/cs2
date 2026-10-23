; The frozen scene PVS caller keeps its current scene object in RSI.
; Preserve the original two arguments, caller stack and all nonvolatile state.
; The C++ adapter checks the exact return address before using captured RSI.
EXTERN PlayerChamsPvsAdapter:PROC
.code
PlayerChamsPvsThunk PROC
    mov r8, rsi
    mov r9, QWORD PTR [rsp]
    jmp PlayerChamsPvsAdapter
PlayerChamsPvsThunk ENDP
END
