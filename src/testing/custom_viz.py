import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns

# Load the performance data
df = pd.read_csv('performance_results/performance_data.csv')

# Set the style
sns.set(style="whitegrid")

# Create a more advanced visualization
plt.figure(figsize=(12, 8))

# Create subplots
fig, axes = plt.subplots(2, 2, figsize=(15, 10))

# 1. Throughput by thread count for each request type
sns.lineplot(data=df, x='Threads', y='Throughput', hue='RequestType', marker='o', ax=axes[0,0])
axes[0,0].set_title('Throughput vs Threads')

# 2. Latency by thread count for each request type
sns.lineplot(data=df, x='Threads', y='Latency', hue='RequestType', marker='o', ax=axes[0,1])
axes[0,1].set_title('Latency vs Threads')

# 3. Barplot comparing request types at max threads
max_threads = df['Threads'].max()
max_thread_data = df[df['Threads'] == max_threads]
sns.barplot(data=max_thread_data, x='RequestType', y='Throughput', ax=axes[1,0])
axes[1,0].set_title(f'Throughput Comparison at {max_threads} Threads')

# 4. Heatmap showing speedup relative to single-thread
pivot_df = df.pivot(index='RequestType', columns='Threads', values='Throughput')
speedup_df = pivot_df.div(pivot_df[1], axis=0)  # Normalize by single thread performance
sns.heatmap(speedup_df, annot=True, fmt='.2f', cmap='viridis', ax=axes[1,1])
axes[1,1].set_title('Speedup Relative to Single Thread')

plt.tight_layout()
plt.savefig('performance_results/advanced_analysis.png')
plt.show()