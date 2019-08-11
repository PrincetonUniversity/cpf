
set terminal pngcairo enhanced font "arial,10" fontscale 1.0 size 1280, 720
set output "misspec.png"

reset

set key rmargin
set style increment default

set grid noxtics
set noxtics
set ytics norangelimit autofreq font ",8"
set xtics border in scale 0,0 nomirror autojustify
# set xtics norangelimit font ",8"
# set xtics ()
set style data histograms
# set style histogram cluster title textcolor lt -1 offset character 0, -1
set style histogram cluster gap 1
# set xlabel offset character 0, -2, 0 font "Helvetica,8" textcolor lt -1 norotate
set ylabel "Speedup over sequential"

set style fill pattern 1 border
# set boxwidth 0.8

set yrange [0:40]
set title "Speedup comparison with Privateer on 28 cores"
plot 'misspec.dat' using 1:xtic(4) title 'No Misspec', '' using 2 title '\~0\.1% Misspec', \
                '' using 3 title '\~0\.2% Misspec'
