import os
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import matplotlib.colors as mcolors

FILENAME = "dump.txt"

if not os.path.exists(FILENAME):
    print(f"Ошибка: Файл '{FILENAME}' не найден!")
    exit(1)

# Считываем данные стандартными средствами (threads -> list of (size, time))
data_by_threads = {}

with open(FILENAME, "r") as f:
    for line in f:
        parts = line.strip().split()
        if not parts:
            continue
        try:
            th = int(parts[0])
            size = int(parts[1])
            time_ms = float(parts[2])
        except ValueError:
            # Пропуск строки с текстовым заголовком
            continue

        if th not in data_by_threads:
            data_by_threads[th] = []
        data_by_threads[th].append((size, time_ms))

if not data_by_threads:
    print("Ошибка: В файле нет валидных данных для построения!")
    exit(1)

threads_list = sorted(data_by_threads.keys())
min_th, max_th = min(threads_list), max(threads_list)

# Настройка цветовой палитры (спектральный градиент от min до max потоков)
cmap = plt.get_cmap("turbo")
norm = mcolors.Normalize(vmin=min_th, vmax=max_th)

fig, ax = plt.subplots(figsize=(12, 7))

for th in threads_list:
    # Сортируем точки по размеру массива перед отрисовкой
    points = sorted(data_by_threads[th], key=lambda x: x[0])
    x_vals = [p[0] for p in points]
    y_vals = [p[1] for p in points]

    color = cmap(norm(th))
    ax.plot(
        x_vals, 
        y_vals, 
        color=color, 
        linewidth=1.3, 
        alpha=0.85, 
        label=f"{th} потоков" if th in [1, 2, 4, 8, 16, 32] else None
    )

# Сетка и подписи
ax.set_title("Сравнение времени сортировки при разном количестве потоков WinAPI", fontsize=13, fontweight="bold", pad=12)
ax.set_xlabel("Размер массива (N)", fontsize=11)
ax.set_ylabel("Время выполнения (мс)", fontsize=11)
ax.grid(True, linestyle="--", alpha=0.6)

# Боковая шкала спектра потоков (Colorbar)
sm = cm.ScalarMappable(cmap=cmap, norm=norm)
sm.set_array([])
cbar = fig.colorbar(sm, ax=ax, pad=0.02)
cbar.set_label("Количество потоков", fontsize=10)
cbar.set_ticks(range(min_th, max_th + 1, max(1, (max_th - min_th) // 8)))

# Компактная легенда только для ключевых степеней двойки (1, 2, 4, 8, 16, 32)
ax.legend(loc="upper left", framealpha=0.9, fontsize=9)

plt.tight_layout()
plt.savefig("benchmark_comparison.png", dpi=300)
print("График успешно сохранен в файл benchmark_comparison.png")
plt.show()