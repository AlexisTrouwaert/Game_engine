// The id pass (milestone 9, part 7), fragment stage: the pick id of the instance (0 for what is not
// selectable: it still hides what is behind it).

uint main(nointerpolation uint id : TEXCOORD0) : SV_Target0 {
    return id;
}
