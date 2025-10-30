%!! This script is written by AI, not me.

% 1. 查找当前目录下的所有 .bmp 文件
bmp_files = glob('*.bmp');

% 2. 检查文件
if isempty(bmp_files)
    disp('在当前目录下没有找到 .bmp 文件。');
    return;
end

% 3. 遍历并处理文件
num_files = length(bmp_files);
disp(['找到 ', num2str(num_files), ' 个 .bmp 文件，开始处理 (确保 24 位真彩色)...']);

for i = 1:num_files
    filename = bmp_files{i};
    disp(['正在处理文件: ', filename]);

    try
        % 读取图像。注意：如果图像是索引图，imread会返回 img 和 map
        [img, map] = imread(filename);

        % --- 关键步骤：确保图像是 M x N x 3 的 RGB 矩阵 ---

        % 检查是否是索引图 (Indexed Image)
        if ~isempty(map)
            % 如果有 Colormap (map)，表示是索引图，需要转换为 RGB
            % ind2rgb 默认返回 double 类型，范围 [0.0, 1.0]
            img_rgb = ind2rgb(img, map);
            disp('  -> 图像为索引图，已使用 ind2rgb 转换为真彩色 RGB (double [0, 1])。');
        % 检查是否是灰度图 (Grayscale Image) - 只有两个维度
        elseif ndims(img) == 2
            % 如果只有 M x N 两个维度，表示是灰度图，需要复制三份通道以生成 24 位 RGB 灰度
            % repmat(A, m, n, p) 复制 A 矩阵 m x n x p 次
            img_rgb = repmat(img, 1, 1, 3);
            disp('  -> 图像为灰度图，已通过 repmat 转换为 24 位 RGB 格式的灰度图。');
        % 否则，已经是 RGB (MxNx3) 或其他格式，我们直接使用它
        else
            img_rgb = img;
            disp('  -> 图像已经是 RGB 或其他多通道格式。');
        end

        % --- 关键步骤：强制转换为 8 位无符号整数 (uint8) ---

        % 检查是否是 double 类型 (可能是 ind2rgb 或 repmat 产生)
        if isfloat(img_rgb) && max(img_rgb(:)) <= 1.0 + eps
            % 如果是 [0.0, 1.0] 范围的浮点数，需要先缩放至 [0, 255]
            img_converted = uint8(round(img_rgb * 255));
            disp('  -> 数据类型为 float [0, 1]，已缩放并转换为 uint8 (8位/颜色分量)。');
        % 检查是否是 uint16 类型 (imread 可能会读入 16 位图)
        elseif isa(img_rgb, 'uint16')
            % 如果是 uint16，需要先缩放至 [0, 255]， uint16 范围是 [0, 65535]，需除以 257
            img_converted = uint8(round(img_rgb / 257));
            disp('  -> 数据类型为 uint16，已缩放并转换为 uint8 (8位/颜色分量)。');
        else
            % 对于 uint8 或其他已在 [0, 255] 范围内的整数类型，直接转换
            img_converted = uint8(img_rgb);
            disp('  -> 数据类型已转换为 uint8 (8位/颜色分量)。');
        end

        % 将 uint8 的 M x N x 3 矩阵保存为 BMP 文件。
        % 在 Octave/MATLAB 中，保存 uint8 的 M x N x 3 矩阵会生成 24 位真彩色 BMP。
        imwrite(img_converted, filename, 'bmp');
        disp('  -> 文件处理完成并已保存为 24 位 BMP。');

    catch ME
        % 错误处理
        disp(['  -> 错误：无法处理文件 ', filename, '。错误信息: ', ME.message]);
    end
end

disp('所有文件处理完毕。');
