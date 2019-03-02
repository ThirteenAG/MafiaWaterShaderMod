ps.1.4
def c0,0,1,0,0
def c1,-2,-2,-2,1
;;def c2, 0.005, 0.076, 0.15, 1 ; default blue
;def c2, 0.125, 0.118, 0.098, 1 ; moskva reka style
;def c2, 0.065, 0.097, 0.124, 1 ; mixed style
def c2, 0, 0, 0.04, 1 ; night style
;def c2, 0.15294117647058823529411764705882, 0.1921568627450980392156862745098, 0.21176470588235294117647058823529,1
; now get c2 from app
def c3,0.8,0.8,0.8,0.8
def c4,1,1,1,1
; c5 = fogColor

texcrd r3.xyz,t2 ; get vecToCam

texld r1,t1  ; get refr
texcrd r0.xyz,t0 ; get refl coords

; make refr bump more powerful
mov r2,r1_bx2
add r2,r2,r2 ; weird way to multiply, couldn't get it working with constant
add r2,r2,r2
add_x2 r2,r2,r2

add r0.xy,r0,r2 ;r1_bx2 ; refract

; sample fresnel
;  get nmap from xyz to xzy - no swizzle for it?
mov r2.xyz,r1 ; copy nmap for fresnel sampling
mov r4.x, r2.y ; tmp=nmap.y

phase

texld r0,r0_dz.xyz ; get refracted reflection and taking projection into account
mov r5,r0

; continue fresnel
mov r2.y, r2.z ; nmap.y = nmap.z
mov r2.z, r4.x ; nmap.z = tmp
dp3_x4_sat r4.xyz, r2_bx2,-r3  ; saturate(dot(normal,-vecToCam) * 4)
; x8 for night

; instead of refraction - toned reflection
lrp r3, c3,   c2,r5

mov r0.xyz,c5

;lrp r0.xyz, r4,   r3,r5 ; lerp by fresnel between pseudorefr and refl
lrp r0.xyz,r4,  r3,c5
;mul r1.rgb,r0,c2
;lrp r0.xyz, c3,  r0,r1

;mov r0.rgb,v0