load alpha.sage

# implements the formula on pages 86-87 of Murphy's thesis
# s is the skewness
# Bf = 1e7; Bg = 5e6; area = 1e16 are the default values used by pol51opt.c
# Bf = 1e11; Bg = 1e11; area = 1e18 # values used for RSA-768
# area is the sieve area, about 2^(2*I-1)*q
# sq is the value of the current special-q (experimental)
def MurphyE(f,g,s=1.0,Bf=1e7,Bg=5e6,area=1e16,K=1000,sq=1,verbose=False):
    df = f.degree()
    dg = g.degree()
    alpha_f = alpha(f,2000)
    alpha_g = alpha(g,2000) # pol51opt.c uses alpha=0 for the linear polynomial
    E = 0
    sx = sqrt(area*s)
    sy = sqrt(area/s)
    for i in range(K):
       theta_i = float(pi/K*(i+0.5))
       xi = cos(theta_i)*sx
       yi = sin(theta_i)*sy
       fi = f(x=xi/yi)*yi^df/sq
       gi = g(x=xi/yi)*yi^dg
       ui = (log(abs(fi))+alpha_f)/log(Bf)
       vi = (log(abs(gi))+alpha_g)/log(Bg)
       v1 = dickman_rho(ui) * dickman_rho(vi)
       if verbose:
          print i, log(abs(fi))+alpha_f, log(abs(gi))+alpha_g, v1
       E += v1
    return E/K

# same as MurphyE, but using numerical integration instead of sampling
def MurphyE_int(f,g,s=1.0,Bf=1e7,Bg=5e6,area=1e16,sq=1):
    df = f.degree()
    dg = g.degree()
    alpha_f = alpha(f,2000)
    alpha_g = alpha(g,2000)
    sx = sqrt(area*s)
    sy = sqrt(area/s)
    var('y,theta')
    xi = cos(theta)*sx
    yi = sin(theta)*sy
    fi = f(x=xi/yi)*yi^df/sq
    gi = g(x=xi/yi)*yi^dg
    ui = (log(abs(fi))+alpha_f)/log(Bf)
    vi = (log(abs(gi))+alpha_g)/log(Bg)
    v1 = dickman_rho(ui) * dickman_rho(vi)
    v1 = v1 / pi # normalization to get same values as MurphyE if v1=1
    return numerical_integral(v1, 0, pi)

# instead of integrating on the half-circle, integrate on the disk
# (this is supposed to give the probability to find a relation)
def MurphyE_int2(f,g,s=1.0,Bf=1e7,Bg=5e6,area=1e16,sq=1):
    df = f.degree()
    dg = g.degree()
    alpha_f = alpha(f,2000)
    alpha_g = alpha(g,2000)
    sx = sqrt(area*s)
    sy = sqrt(area/s)
    var('y,theta,r')
    xi = cos(theta)*sx
    yi = sin(theta)*sy
    fi = f(x=xi/yi)*yi^df/sq*r
    gi = g(x=xi/yi)*yi^dg*r
    ui = (log(abs(fi))+alpha_f)/log(Bf)
    vi = (log(abs(gi))+alpha_g)/log(Bg)
    v1 = dickman_rho(ui) * dickman_rho(vi)
    v1 = v1 * r # integration factor in polar coordinates
    foo = lambda t: numerical_integral(v1(r=t), 0, pi)[0]
    return numerical_integral(foo, 0, 1)

# special code when p divides Res(f,g)
def MurphyE_p(f,g,p,s=1.0,Bf=1e7,Bg=5e6,area=1e16,K=1000,verbose=False):
    df = f.degree()
    dg = g.degree()
    E = 0
    sx = sqrt(area*s)/p
    sy = sqrt(area/s)/p
    x = f.variables()[0]
    y = var('y')
    F = (f(x=x/y)*y^df).expand()
    G = (g(x=x/y)*y^dg).expand()
    alpha_f0 = alpha(f,2000)
    alpha_g0 = alpha(g,2000)
    if verbose:
       print alpha_f0, alpha_g0
    alpha_f0 -= alpha_p_nodisc(f,p)
    alpha_g0 -= alpha_p_nodisc(g,p)
    for xp in range(p): # x -> x*p+xp
       for yp in range(p): # y -> y*p+yp
          if xp == 0 and yp == 0:
             continue
          ef = eg = 0
          Fp = F(x=x*p+xp,y=y*p+yp).expand()
          while ZZ(Fp.content(x)) % p == 0:
             Fp = Fp/p
             ef += 1
          Gp = G(x=x*p+xp,y=y*p+yp).expand()
          while ZZ(Gp.content(x)) % p == 0:
             Gp = Gp/p
             eg += 1
          # we recompute alpha_p for the new f, g
          alpha_f = alpha_f0 + estimate_alpha_p_2(Fp, p, K)
          alpha_g = alpha_g0 + estimate_alpha_p_2(Gp, p, K)
          if verbose:
             print xp, yp, ef, eg, alpha_f, alpha_g
          Ep = 0
	  for i in range(K):
	     theta_i = float(pi/K*(i+0.5))
	     xi = cos(theta_i)*sx
	     yi = sin(theta_i)*sy
	     fi = Fp(x=xi,y=yi)
	     gi = Gp(x=xi,y=yi)
	     ui = (log(abs(fi))+alpha_f)/log(Bf)
	     vi = (log(abs(gi))+alpha_g)/log(Bg)
	     v1 = dickman_rho(ui) * dickman_rho(vi)
             Ep += v1
          if verbose:
             print "x mod p=", xp, "y mod p=", yp, "ef=", ef, "eg=", eg, "Ep=", Ep/K
          E += Ep
    return E/K/(p^2-1)

# example: RSA-768 polynomials
# skewness 44204.72 norm 1.35e+28 alpha -7.30 Murphy_E 3.79e-09
# used Bf = Bg = 1e11 and area = 1e18
# R.<x> = PolynomialRing(ZZ)
# f = 265482057982680*x^6+1276509360768321888*x^5-5006815697800138351796828*x^4-46477854471727854271772677450*x^3+6525437261935989397109667371894785*x^2-18185779352088594356726018862434803054*x-277565266791543881995216199713801103343120
# g=34661003550492501851445829*x-1291187456580021223163547791574810881
# s=44204.72
# Sage code gives MurphyE(f,g,s,1e11,1e11,1e18) = 3.57210195073852e-9

# f1=314460*x^5-6781777312402*x^4-1897893500688827450*x^3+18803371566755928198084581*x^2+2993935114144585946202328612634*x-6740134391082766311453285355967829910
# g1=80795283786995723*x-258705022998682066594976199123
# s1=1779785.90
# gives MurphyE(f1,g1,s1) = 2.21357103514175e-12

# f2=7317291119021626157431*x^4+112772956569732784974895541380*x^3-81294630185701852973301070347631534*x^2-100082786829098214520932412293102900*x-15739142409937639475538548077351461929625
# g2=34661003550492501851445829*x-1291187456580021223163547791574810881
# s2=44204.720
# MurphyE(f2,g2,s2) = 6.65285225657159e-15

# some RSA 896 polynomial
# f3=1062257303520*x^6-2219296729054015122671*x^5-363427049514567418053100636849*x^4-230732633145713229225349965857375907*x^3+1311243309751214417403913343268491664430494*x^2+6573849682197817979582467666900022189225694423288*x+866175084574757676907953813012619055671439344617395520
# g3=98050221285010247116013*x-8539819651788426374365796367618569288023639
# s3=3521536.000
# CADO-NFS gives:
# lognorm: 80.27, alpha: -6.37 (proj: -1.60), E: 73.90, nr: 4
# MurphyE(Bf=10000000,Bg=5000000,area=1.00e+16)=3.60e-19
# MurphyE(f3,g3,s3) gives 3.59802904428366e-19
# MurphyE(f3,g3,s3,1e11,1e11,1e18) gives 1.11463327869894e-10

# another RSA 896 polynomial
# f4=5966748627840*x^6+565433036241905348952*x^5+52398639005476520425660656538*x^4+1235959232443297486815118655819467039*x^3-31018181738027039837856577868864380358157851*x^2-128476324675546615082524287962612958898142759058063*x+422301006437422620589655817160742936774840991440011879641
# g4=159602931489094517610579779*x-6405170522111369624891971785194134958672710
# s4=18259968.000
# CADO-NFS gives:
# lognorm: 80.40, alpha: -11.68 (proj: -3.25), E: 68.73, nr: 4
# MurphyE(Bf=10000000,Bg=5000000,area=1.00e+16)=3.64e-19
# MurphyE(f4,g4,s4) gives 3.64249213433847e-19
# MurphyE(f4,g4,s4,1e11,1e11,1e18) gives 2.25970667729562e-10

# yet another RSA 896 polynomial
# f5=1582400494080*x^6+10754182666712665435035*x^5+2119705994331927639668265988169*x^4-769951555896059233847699860057422828*x^3+56098563755348443319484142873623690549738*x^2+857197338530358973576876468532310408473375340377*x-77399695902207621766439131894884043355415904724450571
# g5=386682491724519608264783*x-7990996094799272260178981062245494003510990
# s5=959232.000
# CADO-NFS gives:
# lognorm: 80.88, alpha: -5.81 (proj: -1.75), E: 75.06, nr: 4
# MurphyE(Bf=10000000,Bg=5000000,area=1.00e+16)=4.15e-19
# MurphyE(f5,g5,s5) gives 4.14971654266668e-19
# MurphyE(f5,g5,s5,1e11,1e11,1e18) gives 9.37611942497951e-11

def rho(x):
   if x < 0:
      raise ValueError, "x < 0"
   if x <= 1.0:
      return 1.0
   if x <= 2.0:
      x = 2 * x - 3.0
      return .59453489206592253066+(-.33333334045356381396+(.55555549124525324224e-1+(-.12345536626584334599e-1+(.30864445307049269724e-2+(-.82383544119364408582e-3+(.22867526556051420719e-3+(-.63554707267886054080e-4+(.18727717457043009514e-4+(-.73223168705152723341e-5+.21206262864513086463e-5*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 3.0:
      x = 2 * x - 5.0
      return .13031956190159661490+(-.11890697945147816983+(.45224027972316855319e-1+(-.97335515428611864000e-2+(.20773419306178919493e-2+(-.45596184871832967815e-3+(.10336375145598596368e-3+(-.23948090479838959902e-4+(.58842414590359757174e-5+(-.17744830412711835918e-5+.42395344408760226490e-6*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 4.0:
      x = 2 * x - 7.0
      return .16229593252987041396e-1+(-.18617080355889960242e-1+(.98231465619823138710e-2+(-.30890609038683816355e-2+(.67860233545741575724e-3+(-.13691972815251836202e-3+(.27142792736320271161e-4+(-.54020882619058856352e-5+(.11184852221470961276e-5+(-.26822513121459348050e-6+.53524419961799178404e-7*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 5.0:
      x = 2 * x - 9.0
      return .13701177421087314589e-2+(-.18032881447340571269e-2+(.11344648599460666353e-2+(-.44785452702335769551e-3+(.12312893763747451118e-3+(-.26025855424244979420e-4+(.49439561514862370128e-5+(-.89908455922138763242e-6+(.16408096470458054605e-6+(-.32859809253342863007e-7+.55954809519625267389e-8*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 6.0:
      x = 2 * x - 11.0
      return .86018611204769414547e-4+(-.12455615869107014099e-3+(.87629281627689306328e-4+(-.39688578340310188782e-4+(.12884593599298603240e-4+(-.31758435723373651519e-5+(.63480088103905159169e-6+(-.11347189099214240765e-6+(.19390866486609035589e-7+(-.34493644515794744033e-8+.52005397653635760845e-9*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 7.0:
      x = 2 * x - 13.0
      return .42503555236283394611e-5+(-.66168162622648216578e-5+(.50451140426428793943e-5+(-.25056278110193975867e-5+(.90780066639285274491e-6+(-.25409423435688445924e-6+(.56994106166489666850e-7+(-.10719440462879689904e-7+(.18244728209885020524e-8+(-.30691539036795813350e-9+.42848520313019466802e-10*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 8.0:
      x = 2 * x - 15.0
      return .17178674964103956208e-6+(-.28335703551339176870e-6+(.23000575057461623263e-6+(-.12233608702949464058e-6+(.47877500071532198696e-7+(-.14657787264925438894e-7+(.36368896326425931590e-8+(-.74970784803203153691e-9+(.13390834297490308260e-9+(-.22532330111335516039e-10+.30448478436493554124e-11*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 9.0:
      x = 2 * x - 17.0
      return .58405695885954012372e-8+(-.10105102932563601939e-7+(.86312378192120507636e-8+(-.48483948355429166070e-8+(.20129738917787451004e-8+(-.65800969013673567377e-9+(.17591641970556292848e-9+(-.39380340623747158286e-10+(.75914903051312238224e-11+(-.13345077002021028171e-11+.18138420114766605833e-12*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   if x <= 10.0:
      x = 2 * x - 19.0
      return .17063527516986966493e-9+(-.30739839907061929947e-9+(.27401311519313991860e-9+(-.16103964872970941164e-9+(.70152211692303181302e-10+(-.24143747383189013980e-10+(.68287507297199575436e-11+(-.16282751120203734207e-11+(.33676191519721199042e-12+(-.63207992345353337190e-13+.88821884863349801119e-14*x)*x)*x)*x)*x)*x)*x)*x)*x)*x
   return 0.277017183772596 * x^(-x)

def check_rho(a,b,N):
   maxerr = -1
   for i in range(N):
      x = a + (b-a)*random()
      y = dickman_rho(x)
      z = rho(x)
      err = abs(z-y)
      if err>maxerr:
         maxerr=err
         print x, err

def expected_growth(f, g, i):
   s = skew_l2norm_tk_circular(f)
   n = l2norm_tk_circular(ff,s)
   # negative side
   kmin = -1
   NORM_MARGIN = 0.2
   while True:
      ff = f + kmin*x^i*f
      n2 = l2norm_tk_circular(ff,s)
      if n2 > n + NORM_MARGIN:
         break
      kmin = 2*kmin
   kmax = kmin/2 # larger than kmin since kmin < 0
   # dichotomy on [kmin,kmax]
   while kmin + 1 < kmax:
      k = (kmin + kmax) // 2
      ff = f + k*x^i*f
      n2 = l2norm_tk_circular(ff,s)
      if n2 > n + NORM_MARGIN: # k is too large (in absolute value)
         kmin = k
      else:
         kmax = k
   rmin = kmax
   # positive side
   kmax = 1
   while True:
      ff = f + kmax*x^i*f
      n2 = l2norm_tk_circular(ff,s)
      if n2 > n + NORM_MARGIN:
         break
      kmin = 2*kmin
   kmin = kmax/2
   # dichotomy on [kmin,kmax]
   while kmin + 1 < kmax:
      k = (kmin + kmax) // 2
      ff = f + k*x^i*f
      n2 = l2norm_tk_circular(ff,s)
      if n2 > n + NORM_MARGIN: # k is too large (in absolute value)
         kmax = k
      else:
         kmin = k
      rmax = kmin

def expected_rotation_gain (f, g):
   S = 1.0
