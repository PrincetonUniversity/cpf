set terminal pngcairo enhanced font "arial,10" fontscale 1.0 size 1280, 720
set output "overheads.png"

set key rmargin
set style increment default

set grid noxtics
set noxtics
set ytics norangelimit autofreq font ",8"
# set xtics border in scale 0,0 nomirror rotate by -45 autojustify
# set xtics norangelimit font ",8"
# set xtics ()
set style data histograms
set style histogram columnstacked title textcolor lt -1 offset character 0, -1
# set xlabel offset character 0, -2, 0 font "Helvetica,8" textcolor lt -1 norotate
set ylabel "Worker execution time normalized to LSD Useful Work"

set style fill solid noborder
# set boxwidth 0.8

set title "Overhead comparison with Privateer"
plot newhistogram "2mm" lt 1, 'overheads.dat' using 1, '' using 2, \
     newhistogram "3mm" lt 1, 'overheads.dat' using 3, '' using 4, \
     newhistogram "gemm" lt 1, 'overheads.dat' using 5, '' using 6, \
     newhistogram "dijkstra" lt 1, 'overheads.dat' using 7, '' using 8, \
     newhistogram "052.alvinn" lt 1, 'overheads.dat' using 9, '' using 10, \
     newhistogram "179.art" lt 1, 'overheads.dat' using 11, '' using 12, \
     newhistogram "blackscholes" lt 1, 'overheads.dat' using 13, '' using 14, \
     newhistogram "swaptions" lt 1, 'overheads.dat' using 15, '' using 16, \
     newhistogram "doitgen" lt 1, 'overheads.dat' using 17, '' using 18, \
     newhistogram "enc-md5" lt 1, 'overheads.dat' using 19, '' using 20, \

# plot 'overheads.dat' using 1 title '2mm', '' using 2 title '2mm priv'