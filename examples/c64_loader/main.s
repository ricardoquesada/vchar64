;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
;
; VChar64 c64 loader example
;
; Compile it using ca65 (http://cc65.github.io/cc65/)
;
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;

.segment "CODE"

        jsr clear_screen

        lda #0
        sta $d020                       ; border color
        lda #10
        sta $d021                       ; background color (from VChar64)
        lda #15
        sta $d022                       ; multicolor #1 (from VChar64)
        lda #0
        sta $d023                       ; multicolor #2 (from VChar64)

        lda #%00011000                  ; no scroll, multi-color,40-cols
        sta $d016

        lda #%00011110                  ; charset at $3800
        sta $d018

        jsr setup_charset
        jsr display_logo

        jmp *                           ; infinite loop

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; clear_screen
; clears screen RAM and color RAM
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
.proc clear_screen
        ; "jsr $e544" cleans the screen using $20
        ; but since we are using a custom charset, we should use 
        ; $67 (103) as the clear char
        ; also, we should clear the color ram as well

        lda #$67                        ; screen code
        ldx #$00
loop1:  sta $0400,x                     ; clears the screen memory
        sta $0500,x
        sta $0600,x
        sta $06e8,x
        inx
        bne loop1

        lda colors + $67               ; get color for tile $67
        ldx #$00
loop2:  sta $d800,x                    ; clears the color RAM
        sta $d900,x
        sta $da00,x
        sta $dae8,x
        inx
        bne loop2

        rts
.endproc

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; setup_charset
; copies charset to $3800
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
.proc setup_charset
        ; copies the charset to $3800
        ; The alternative, is to import the charset data directly to $3800

        ldy #7                          ; 256 * 8 = 2048 bytes to copy
outer_loop:
        ldx #0
inner_loop:
src_hi = * + 2
        lda charset,x
dst_hi = * + 2
        sta $3800,x
        dex
        bne inner_loop
        inc src_hi
        inc dst_hi
        dey
        bpl outer_loop
        rts
        
.endproc

;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
; display_logo
; copies logo and colors to screen RAM and color RAM
;=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-;
.proc display_logo
        ; displays the logo: 40x10 (400 bytes)
        ; with an unrolled loop: copies 256 + 144 chars

        ; copies 256 chars
        ldx #$00
loop:   lda map + $0000,x
        sta $0400 + $0000,x             ; screen chars

        ; copies its color
        tay
        lda colors,y
        sta $d800,x                     ; colors for the chars

        ; copies 256 chars as well, but overwrites
        ; some of the previous one. it copies 144 new chars
        lda map + (MAP_COUNT .MOD 256),x
        sta $0400 + (MAP_COUNT .MOD 256),x ; screen chars

        ; copies its colors
        tay
        lda colors,y
        sta $d800 + (MAP_COUNT .MOD 256),x ; colors for the chars

        inx
        bne loop

        rts
.endproc

.include "logo-colors.s"
.include "logo-map.s"
.include "logo-charset.s"
