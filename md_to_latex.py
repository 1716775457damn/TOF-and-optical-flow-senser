import re
import os

def escape_latex(text):
    """转义LaTeX特殊字符"""
    chars = {
        '&': r'\&',
        '%': r'\%',
        '$': r'\$',
        '#': r'\#',
        '_': r'\_',
        '{': r'\{',
        '}': r'\}',
        '~': r'\textasciitilde{}',
        '^': r'\^{}',
        '\\': r'\textbackslash{}',
    }
    pattern = re.compile('|'.join(re.escape(key) for key in chars.keys()))
    return pattern.sub(lambda match: chars[match.group(0)], text)

def md_to_latex(md_file, latex_file):
    """将Markdown文件转换为LaTeX格式"""
    # 读取Markdown文件
    with open(md_file, 'r', encoding='utf-8') as f:
        md_content = f.read()
    
    # 创建LaTeX文件头部
    latex_content = [
        r'\documentclass[12pt, a4paper]{article}',
        r'\usepackage{xeCJK}',  # 使用xeCJK代替ctex，更好地处理中文
        r'\usepackage{geometry}',
        r'\geometry{a4paper, margin=1in}',
        r'\usepackage{graphicx}',
        r'\usepackage{booktabs}',
        r'\usepackage{longtable}',
        r'\usepackage{hyperref}',
        r'\usepackage{xcolor}',
        r'\usepackage{tikz}',
        r'\usetikzlibrary{shapes,arrows,positioning}',
        r'',
        # 配置listings包以更好地支持中文和代码
        r'\usepackage{listings}',
        r'\lstset{',
        r'  basicstyle=\ttfamily\small,',
        r'  breaklines=true,',
        r'  columns=flexible,',
        r'  frame=single,',
        r'  keepspaces=true,',
        r'  showstringspaces=false,',
        r'  extendedchars=true,',
        r'  texcl=true,',  # 允许在代码中使用LaTeX注释
        r'}',
        r'',
        r'\title{基于神经网络预测的飞控系统闭环控制技术文档}',
        r'\author{}',
        r'\date{\today}',
        r'',
        r'\begin{document}',
        r'\maketitle',
        r'\tableofcontents',
        r'\newpage',
    ]
    
    # 处理标题
    lines = md_content.split('\n')
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        
        # 处理标题
        header_match = re.match(r'^(#{1,6})\s+(.+)$', line)
        if header_match:
            level = len(header_match.group(1))
            title = escape_latex(header_match.group(2))
            if level == 1:
                latex_content.append(r'\section{' + title + '}')
            elif level == 2:
                latex_content.append(r'\subsection{' + title + '}')
            elif level == 3:
                latex_content.append(r'\subsubsection{' + title + '}')
            else:
                latex_content.append(r'\paragraph{' + title + '}')
            i += 1
            continue
        
        # 处理表格
        if line.startswith('|') and i + 1 < len(lines) and lines[i+1].strip().startswith('|'):
            # 获取表格列数
            header_cells = [cell.strip() for cell in line.split('|')[1:-1]]
            num_cols = len(header_cells)
            
            # 开始表格环境
            latex_content.append(r'\begin{longtable}{' + '|c' * num_cols + '|}')
            latex_content.append(r'\hline')
            
            # 添加表头
            latex_content.append(' & '.join([escape_latex(cell) for cell in header_cells]) + r' \\')
            latex_content.append(r'\hline')
            
            # 跳过分隔行
            i += 2
            
            # 添加表格内容
            while i < len(lines) and lines[i].strip().startswith('|'):
                cells = [cell.strip() for cell in lines[i].split('|')[1:-1]]
                if len(cells) == num_cols:  # 确保列数匹配
                    latex_content.append(' & '.join([escape_latex(cell) for cell in cells]) + r' \\')
                    latex_content.append(r'\hline')
                i += 1
            
            # 结束表格环境
            latex_content.append(r'\end{longtable}')
            continue
        
        # 处理Mermaid流程图
        if line == '```mermaid':
            mermaid_content = []
            i += 1
            while i < len(lines) and lines[i] != '```':
                mermaid_content.append(lines[i])
                i += 1
            
            # 添加流程图占位符
            latex_content.append(r'\begin{center}')
            latex_content.append(r'\fbox{\parbox{0.8\textwidth}{\centering\textbf{此处应有流程图} \\[0.5em] 请参考原Markdown文件中的Mermaid代码}}')
            latex_content.append(r'\end{center}')
            
            i += 1  # 跳过结束的 ```
            continue
        
        # 处理代码块 - 改进版本，避免中文注释问题
        if line == '```' or line.startswith('```'):
            language = line[3:].strip() if line.startswith('```') and len(line) > 3 else ''
            code_content = []
            i += 1
            while i < len(lines) and lines[i] != '```':
                code_content.append(lines[i])
                i += 1
            
            # 使用verbatim环境代替lstlisting处理代码，避免中文注释问题
            latex_content.append(r'\begin{verbatim}')
            latex_content.append('\n'.join(code_content))
            latex_content.append(r'\end{verbatim}')
            
            i += 1  # 跳过结束的 ```
            continue
        
        # 处理普通段落
        if line:
            latex_content.append(escape_latex(line) + r'\\')
        else:
            latex_content.append('')
        
        i += 1
    
    # 添加LaTeX文件尾部
    latex_content.append(r'\end{document}')
    
    # 写入LaTeX文件
    with open(latex_file, 'w', encoding='utf-8') as f:
        f.write('\n'.join(latex_content))
    
    print(f"已将 {md_file} 转换为 {latex_file}")

if __name__ == "__main__":
    md_file = "1.md"
    latex_file = "神经网络飞控系统文档.tex"
    md_to_latex(md_file, latex_file) 