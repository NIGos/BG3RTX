"""Independent double-precision native pool sun BRDF and footprint reference.

The constants and operation order follow captured pool FS c4e92ccdb08dd777,
SSA 3227..3316. This deliberately keeps the game's representative solar disc;
it does not replace it by a point light, normalized GGX D alone, or Schlick(N.V).
"""
import math


def dot(a, b):
    return math.fsum(x*y for x, y in zip(a,b))


def add(a,b):
    return tuple(x+y for x,y in zip(a,b))


def mul(a,s):
    return tuple(x*s for x in a)


def norm(a):
    return mul(a, 1/math.sqrt(dot(a,a)))


def sat(x):
    return max(0.0,min(1.0,x))


def native_sun_spec(n, v, light, roughness, f0=.0196):
    """Unit incident radiance; clouds/visibility/exposure multiply this outside.

    n/v/light are unit world-space vectors. Native derivative AA roughness is
    supplied unchanged. As in the FS, N.L uses the sun centre, whereas D/F/G
    use the representative direction on the native finite solar disc.
    """
    n, v, light = norm(n),norm(v),norm(light)
    nol = sat(dot(n,light))
    if nol == 0:
        return 0.0
    reflected=add(v,mul(n,-2*dot(n,v)))
    cosine=dot(light,reflected)
    if cosine < .999989449977874755859375:
        perpendicular=add(reflected,mul(light,-cosine))
        if dot(perpendicular,perpendicular)>0:
            representative=norm(add(mul(light,.999989449977874755859375),
                                    mul(norm(perpendicular),.0045900647528469562530517578125)))
        else:
            # The shader's normalization is undefined at the antipode. Real
            # fixtures avoid this unresolved branch rather than invent data.
            raise ValueError('native representative direction undefined')
    else:
        representative=reflected
    h=norm(add(v,representative))
    noh=sat(dot(n,h)); nov=max(dot(n,v),9.9999997473787516355514526367188e-6)
    nlr=sat(dot(n,representative)); loh=sat(dot(representative,h))
    k=((roughness+1)*.5)**2
    a4=roughness**4
    denominator=1+(a4-1)*noh*noh
    visibility=.5/(nlr*math.sqrt(nov*nov*(1-k)+k)+nov*math.sqrt(nlr*nlr*(1-k)+k))
    fresnel=f0+(1-f0)*max(abs(1-loh),9.9999997473787516355514526367188e-5)**5
    return nol*fresnel*visibility*a4*.3183098733425140380859375/(denominator*denominator)


def footprint_reference(normal_at, v, light, roughness, subdivisions, f0=.0196):
    """Independent midpoint supersampling of the unit square pixel footprint."""
    return math.fsum(native_sun_spec(normal_at((x+.5)/subdivisions,(y+.5)/subdivisions),
                                     v,light,roughness,f0)
                     for y in range(subdivisions) for x in range(subdivisions))/(subdivisions**2)


def half_encoding_bound(value):
    """One binary16 ULP for the separated direct/rest record (not a fitted %)."""
    if value == 0:
        return 2**-24
    return 2**(max(-14,math.floor(math.log2(abs(value))))-10)
