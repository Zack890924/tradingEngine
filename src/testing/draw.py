import matplotlib.pyplot as plt


threads = [1, 2, 4]
throughput = [1233.99, 1487.99, 1966.82]  # TPS


plt.figure(figsize=(8, 5))
plt.plot(threads, throughput, marker='o', linestyle='-', color='blue', linewidth=2)


plt.xlabel('Cores (Threads)', fontsize=12)
plt.ylabel('Throughput (TPS)', fontsize=12)
plt.title('Throughput vs Number of Cores', fontsize=14)
plt.grid(True)


plt.savefig("throughput_vs_cores.png")
plt.show()
print("Saved figure to throughput_vs_cores.png")
