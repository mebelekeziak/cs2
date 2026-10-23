EXTERN PlayerChamsPvsThunk:PROC
PUBLIC PlayerChamsPvsFixtureReturn
.code
PlayerChamsPvsFixture PROC FRAME
    push rsi
    .pushreg rsi
    sub rsp, 20h
    .allocstack 20h
    .endprolog
    mov rsi, r8
    call PlayerChamsPvsThunk
PlayerChamsPvsFixtureReturn LABEL BYTE
    add rsp, 20h
    pop rsi
    ret
PlayerChamsPvsFixture ENDP
END
