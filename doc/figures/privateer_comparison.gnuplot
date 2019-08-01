set terminal pngcairo enhanced font "arial,10" fontscale 1.0 size 1280, 720
set output "comparison.png"

set key rmargin
set style increment default

set grid noxtics
set noxtics
set ytics norangelimit autofreq font ",8"
# set xtics border in scale 0,0 nomirror rotate by -45 autojustify
# set xtics norangelimit font ",8"
# set xtics ()
set style data histograms
# set xlabel offset character 0, -2, 0 font "Helvetica,8" textcolor lt -1 norotate
set ylabel "Speedup over sequential"

set style fill solid noborder
# set boxwidth 0.8

# set yrange [:120]
set title "Speedup comparison with Privateer"
plot newhistogram "2mm" lt 1, 'overheads_scaled.raw' using 1, '' using 2, \
