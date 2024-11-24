import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("benchmark_results.txt", names=["test_type", "threads", "time"])
df["threads"] = pd.to_numeric(df["threads"], errors="coerce")
df["time"] = pd.to_numeric(df["time"], errors="coerce")
# df = df.dropna()

insert_data = df[df["test_type"].str.contains("insert")]
mixed_data = df[df["test_type"].str.contains("mixed")]

# insert-only
plt.figure(figsize=(10, 6))
for test_type in insert_data["test_type"].unique():
    subset = insert_data[insert_data["test_type"] == test_type]
    plt.plot(subset["threads"].values, subset["time"].values, label=test_type)

plt.title("Insert-only Benchmark")
plt.xlabel("Threads")
plt.ylabel("Time (ms)")
plt.xscale("log", base=2)
plt.legend()
plt.grid(True)
plt.savefig("insert_only_benchmark.png")
plt.show()

# mixed
plt.figure(figsize=(10, 6))
for test_type in mixed_data["test_type"].unique():
    subset = mixed_data[mixed_data["test_type"] == test_type]
    plt.plot(subset["threads"].values, subset["time"].values, label=test_type)

plt.title("Mixed Benchmark")
plt.xlabel("Threads")
plt.ylabel("Time (ms)")
plt.xscale("log", base=2)
plt.legend()
plt.grid(True)
plt.savefig("mixed_benchmark.png")
plt.show()
