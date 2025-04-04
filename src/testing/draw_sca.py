import matplotlib.pyplot as plt


cores = [1, 2, 4]
tps = [3500, 7160, 11500]


efficiency = [t / c for t, c in zip(tps, cores)]

plt.figure(figsize=(8, 5))
plt.plot(cores, tps, marker='o', label='Throughput (TPS)', linewidth=2)


plt.plot(cores, efficiency, marker='s', linestyle='--', label='Efficiency (TPS/Core)', linewidth=2)

plt.title("Scalability vs. CPU Cores")
plt.xlabel("Number of CPU Cores")
plt.ylabel("TPS / Efficiency")
plt.xticks(cores)
plt.grid(True)
plt.legend()
plt.tight_layout()
plt.savefig("scalability_plot.png")
plt.show()
