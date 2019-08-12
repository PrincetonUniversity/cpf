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

set style fill solid border -1
# set boxwidth 0.8

set title "Overhead comparison with Privateer"
plot newhistogram "2mm" lt 2, 'overheads.dat' using 1, '' using 2, '' using 3, \
     newhistogram "3mm" lt 2, 'overheads.dat' using 4, '' using 5, '' using 6, \
     newhistogram "gemm" lt 2, 'overheads.dat' using 7, '' using 8, '' using 9, \
     newhistogram "dijkstra" lt 2, 'overheads.dat' using 10, '' using 11, '' using 12, \
     newhistogram "052.alvinn" lt 2, 'overheads.dat' using 13, '' using 14, '' using 15, \
     newhistogram "179.art" lt 2, 'overheads.dat' using 16, '' using 17, '' using 18, \
     newhistogram "blackscholes" lt 2, 'overheads.dat' using 19, '' using 20, '' using 21, \
     newhistogram "swaptions" lt 2, 'overheads.dat' using 22, '' using 23, '' using 24, \
     newhistogram "doitgen" lt 2, 'overheads.dat' using 25, '' using 26, '' using 27, \
     newhistogram "enc-md5" lt 2, 'overheads.dat' using 28, '' using 29, '' using 30, \
     newhistogram "correlation" lt 2, 'overheads.dat' using 31, '' using 32, '' using 33, \
     newhistogram "covariance" lt 2, 'overheads.dat' using 34, '' using 35, '' using 36

# plot 'overheads.dat' using 1 title '2mm', '' using 2 title '2mm priv'
