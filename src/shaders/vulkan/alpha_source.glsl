// Same unassociation and opacity arithmetic in native and compute.
vec4 pui_alpha_source(vec4 associated,float opacity) {
    return associated.a==0.0?vec4(0.0):vec4(associated.rgb/associated.a,associated.a*opacity);
}
