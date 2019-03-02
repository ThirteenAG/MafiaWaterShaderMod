vs.1.0              ; version instruction
def c10,0.5,0.5,0.5,0.5

; c0-3 = MatWorldViewProj
; c4-7 = MatWorld
; c12 = posCam
; c13-16 = MatView

; fog far = 10; fog near = 0
; c12 = 1/(fogfar-fognear), fogfar/(fogfar-fognear), 0, 1
;def c17, 1, 1, 0,1
;def c17, 0, 1, 0, 0

m4x4 r0, v0, c0   ; transform vertices by view/projection matrix
mov oPos, r0      ;export Pos

;0.5f*(OUT.Position.w+OUT.Position.x);
add r0.x,r0.x,r0.w
mul r0.x,r0.x,c10

;0.5f*(OUT.Position.w-OUT.Position.y);
sub r0.y,r0.w,r0.y
mul r0.y,r0.y,c10

mov r0.z,r0.w  ; from xyzw to xyww
mov oT0, r0    ; export xyww to TC0

m4x4 r1,v0,c4    ; get world pos
mul r2,r1,c11     ; control tiling
mov oT1.xy,r2.xz ; export tiled world xz to TC1

; normalize(worldPos - posCam)
; normalize = v / sqrt(dot(v,v));
; normalize = v * (1/sqrt(dot(v,v)));
; normalize = v * rsqrt(dot(v,v));

sub r1, r1,c12  ; worldPos - posCam
dp3 r2.x, r1.xyz,r1.xyz
rsq r2.xyzw,r2.x
mul r1.xyz,r1.xyz,r2.xyz  ; r1.xyz = vecToCam
mov oT2.xyz,r1.xyz  ; export vecToCam to TC2

;dp3 r0.x,r1,c17
;min r0.x,r0.x,c17.x
;max oFog.x,r0.x,c17.y

;rcp r0,r2
;mov oFog.x,r0.x
;mul oFog.x,r0.x,c17.x