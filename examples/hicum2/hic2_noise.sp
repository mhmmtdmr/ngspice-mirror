HICUM2v2.34 Noise Test

vcc 4 0 26
vin 1 0 ac 1

ccouple 1 b 1

ibias 0 b 100uA

rload 4 c 1k noisy=0

q1 c b 0 0 hicumL2V2p3x

.include model-card-examples.lib

.control
op
print all
echo
noise v(c) vin dec 10 10 10G 1
setplot
setplot noise1
plot ally
plot 300*inoise_spectrum onoise_spectrum
setplot noise2
print all
echo
print inoise_total onoise_total
.endc

.end
