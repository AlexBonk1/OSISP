import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

df = pd.read_csv("results.csv")
grouped = df.groupby("config", sort=False).mean(numeric_only=True).reset_index()

configs = grouped["config"].tolist()
x = np.arange(len(configs))

plt.figure(figsize=(8, 5))
bars = plt.bar(x, grouped["elapsed_ms"], color=["#4C72B0", "#55A868", "#C44E52"], width=0.5)
plt.xticks(x, configs)
plt.xlabel("Конфигурация рабочих каналов (Ступень 1 - Ступень 2 - Ступень 3)")
plt.ylabel("Среднее время выполнения, мс")
plt.title("Зависимость общего времени обработки от конфигурации каналов")
plt.grid(axis="y", linestyle="--", alpha=0.6)
for bar in bars:
    h = bar.get_height()
    plt.text(bar.get_x() + bar.get_width() / 2, h + 15, f"{h:.1f} мс", ha="center", va="bottom")
plt.tight_layout()
plt.savefig("chart1_execution_time.png", dpi=200)
plt.close()

width = 0.25
plt.figure(figsize=(9, 5))
plt.bar(x - width, grouped["avg_wait1_ms"], width, label="Ступень 1", color="#4C72B0")
plt.bar(x, grouped["avg_wait2_ms"], width, label="Ступень 2", color="#DD8452")
plt.bar(x + width, grouped["avg_wait3_ms"], width, label="Ступень 3", color="#55A868")
plt.xticks(x, configs)
plt.xlabel("Конфигурация рабочих каналов")
plt.ylabel("Среднее время ожидания семафора, мс")
plt.title("Среднее время ожидания по ступеням (выявление узкого места)")
plt.legend()
plt.grid(axis="y", linestyle="--", alpha=0.6)
plt.tight_layout()
plt.savefig("chart2_stage_wait_time.png", dpi=200)
plt.close()