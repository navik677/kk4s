*start|Start
@multi a=1 \
;   b=2 \
;   c=3
[pmacro name=pm x=10 y=20]
@usepm pm
[tag2 z=1 y=2 x=3]
[macro name=m2][inner p=1 *][endmacro]
[m2 q=2]
@emb exp="'[nested k=v]'" escape=false
[last]
