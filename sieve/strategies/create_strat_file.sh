#!/bin/bash
# usage: ./create_strat_file.sh <lim0> <lpb0> <mfb0> <lim1> <lpb1> <mfb1> <poly> <I> <cado_bindir>

# Remark: lim is the factor base bound, fbb is the log in base 2 of it.

if [ $# != 9 ]; then
    echo "usage: ./create_strat_file.sh <lim0> <lpb0> <mfb0> <lim1> <lpb1> <mfb1> <poly> <I> <cado_bindir>";
    exit;
fi

lim0=$1
lpb0=$2
mfb0=$3
lim1=$4
lpb1=$5
mfb1=$6
poly=$7
I=$8
cadodir=$9

lim=$(($lim0>$lim1?$lim1:$lim0))
lpb=$(($lpb0>$lpb1?$lpb1:$lpb0))
fbb=`echo "l($lim)/l(2)" | bc -l | cut -d "." -f 1`

GFM=$cadodir/sieve/strategies/gfm
BENCHFM=$cadodir/sieve/strategies/benchfm
GST=$cadodir/sieve/strategies/gst
MAKEFB=$cadodir/sieve/makefb
LAS=$cadodir/sieve/las

for m in PM1 ECM-M12 ECM-M16 ECM-B12 PP1-27 PP1-65; do 
    rm -f data_${m}_*
done

x=0
while read m b1min b1max b1step cmin cmax cstep; do
    outfile=`mktemp -p . data_${m}_XXXXXXX`
    sleep 1
    cmd="$GFM -lb $fbb -ub $lpb  -m $m -b1min $b1min -b1max $b1max -b1step $b1step -cmin $cmin -cmax $cmax -cstep $cstep -out $outfile"
    echo $cmd
    let x=$x+1
    if [ $x == 10 ]; then
        x=0
        $cmd
    else
        $cmd &
    fi
done << EOF
PM1      50   300   20  10  50   5 
PM1      300  600   20  10  50   5 
PM1      50   300   20  50  200  10 
PM1      300  600   20  50  200  10 
PM1      600  1000  20  10  50   5 
PM1      600  1000  20  50  200  10
ECM-M12  50   300   20  10  50   5 
ECM-M12  300  600   20  10  50   5 
ECM-M12  50   300   20  50  300  10 
ECM-M12  300  600   20  50  300  10 
ECM-M12  600  1500  20  10  50   5 
ECM-M12  600  1500  20  50  300  10
ECM-M16  50   300   20  10  50   5 
ECM-M16  300  600   20  10  50   5 
ECM-M16  50   300   20  50  300  10 
ECM-M16  300  600   20  50  300  10 
ECM-M16  600  1500  20  10  50   5 
ECM-M16  600  1500  20  50  300  10 
ECM-B12  50   300   20  10  50   5 
ECM-B12  300  600   20  10  50   5 
ECM-B12  50   300   20  50  300  10 
ECM-B12  300  600   20  50  300  10 
ECM-B12  600  1500  20  10  50   5 
ECM-B12  600  1500  20  50  300  10 
PP1-27   50   300   20  10  50   5 
PP1-27   300  600   20  10  50   5 
PP1-27   50   300   20  50  200  10 
PP1-27   300  600   20  50  200  10 
PP1-27   600  1500  20  10  50   5 
PP1-27   600  1500  20  50  200  10 
PP1-65   50   300   20  10  50   5 
PP1-65   300  600   20  10  50   5 
PP1-65   50   300   20  50  200  10 
PP1-65   300  600   20  50  200  10 
PP1-65   600  1500  20  10  50   5 
PP1-65   600  1500  20  50  200  10
EOF

wait

cat data_PM1_* > data_PM1
cat data_PP1-27_* > data_PP1_27
cat data_PP1-65_* > data_PP1_65
cat data_ECM-M12_* > data_ECM_M12
cat data_ECM-M16_* > data_ECM_M16
cat data_ECM-B12_* > data_ECM_B12

$GFM -fch -fch_in data_PP1_27 -fch_out data_PP1_27_ch
$GFM -fch -fch_in data_PM1 -fch_out data_PM1_ch
$GFM -fch -fch_in data_PP1_65 -fch_out data_PP1_65_ch
$GFM -fch -fch_in data_ECM_M12 -fch_out data_ECM_M12_ch
$GFM -fch -fch_in data_ECM_M16 -fch_out data_ECM_M16_ch
$GFM -fch -fch_in data_ECM_B12 -fch_out data_ECM_B12_ch

# for m in PM1 ECM-M12 ECM-M16 ECM-B12 PP1-27 PP1-65; do 
#     rm -f data_${m}_*
# done

for XXX in PP1_27 PM1 PP1_65 ECM_M12 ECM_M16 ECM_B12; do 
    $BENCHFM -in data_${XXX}_ch -p -t -lb $fbb -out data_${XXX}_pt
done

cat data_PP1_65_pt data_PP1_27_pt > data_PP1_pt
cat data_ECM_M12_pt data_ECM_B12_pt > data_ECM_RC_pt

# Filtering (???)
for XXX in PP1 PM1 ECM_M16 ECM_RC; do 
    $BENCHFM -f 10 -in data_${XXX}_pt -lb $fbb -out ${XXX}
done
cat PP1 PM1 ECM_M16 ECM_RC > All_methods

# Generate decompositions
mkdir decomp
xmin=`echo "l($lim0*$lim0)/l(2)" | bc -l | cut -d "." -f 1`
for x in `seq $xmin $mfb0`; do
    $GST -gdc -lim0 $lim0 -mfb0 $x -out decomp/decomp_${lim0}_${x}
done
xmin=`echo "l($lim1*$lim1)/l(2)" | bc -l | cut -d "." -f 1`
for x in `seq $xmin $mfb1`; do
    $GST -gdc -lim0 $lim1 -mfb0 $x -out decomp/decomp_${lim1}_${x}
done

# precompute strategies
mkdir res_precompt_st
for r0 in `seq 0 $mfb0`; do
    $GST -gst_r -lim0 $lim0 -lpb0 $lpb0 -r0 $r0 -ncurves 20 -in All_methods -decomp decomp -out res_precompt_st
done
for r0 in `seq 0 $mfb1`; do
    $GST -gst_r -lim0 $lim1 -lpb0 $lpb1 -r0 $r0 -ncurves 20 -in All_methods -decomp decomp -out res_precompt_st
done

# create strategies
mkdir res_matrix
for r0 in `seq 0 $mfb0`; do
    for r1 in `seq 0 $mfb1`; do
        $GST -gst -mfb0 $mfb0 -lim0 $lim0 -mfb1 $mfb1 -lim1 $lim1 -r0 $r0 -r1 $r1 -in res_precompt_st -out res_matrix
    done
done

#collect a sample of the distribution of the pairs of cofactors.
$MAKEFB -poly $poly -alim $lim0 -maxbits $I -out XXXX.roots0 -side 0
$MAKEFB -poly $poly -alim $lim1 -maxbits $I -out XXXX.roots1 -side 1
q0=$((4*$lim1))
q1=$(($q0+1000))
$LAS -poly $poly -I $I -fb0 XXXX.roots0 -fb1 XXXX.roots1 -lim0 $lim0 -lim1 $lim1 -lpb0 $lpb0 -lpb1 $lpb1 -mfb0 $mfb0 -mfb1 $mfb1 -t 2 -stats-cofact cofactors.stats -q0 $q0 -q1 $q1 -out XXXX.out

line=`grep "Total cpu time" XXXX.out`
t0=`echo $line | cut -d " " -f 5 | cut -d s -f 1`
t1=`echo $line | cut -d " " -f 19 | cut -d ")" -f 1`
t=$(($t0-$t1))

# emulator
$FINALST -st res_matrix -dist cofactors.stats -t $t -mfb0 $mfb0 -mfb1 $mfb1 final_st
