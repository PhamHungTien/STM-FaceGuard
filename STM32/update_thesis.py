import os
import re

# Update Chapter 4
ch4_path = '../thesis/chapters/ch4_software.tex'
ch4_content = r"""% ============================================================
%  CHƯƠNG 4: THIẾT KẾ PHẦN MỀM
% ============================================================
\chapter{Thiết kế phần mềm}
\label{chap:software}

\section{Kiến trúc phần mềm tổng quan}
\label{sec:sw_architecture}

Phần mềm của hệ thống được thiết kế theo kiến trúc hướng sự kiện (Event-driven) trên nền tảng Dual-MCU. Sự tách biệt giữa tầng xử lý trí tuệ nhân tạo (ESP32-S3) và tầng điều phối ngoại vi (STM32F303RE) giúp tối ưu hóa tài nguyên phần cứng, đảm bảo tính ổn định và khả năng mở rộng của hệ thống:

\begin{itemize}
  \item \textbf{STM32 Firmware}: Đảm nhận vai trò "Bộ não điều phối". Firmware được viết bằng C thuần túy sử dụng thư viện HAL, hoạt động theo kiến trúc vòng lặp siêu vòng (\textit{superloop}) không chặn (non-blocking). Việc tích hợp DMA (Direct Memory Access) giúp xử lý luồng dữ liệu ngoại vi tốc độ cao mà không làm gián đoạn các tác vụ hiển thị giao diện hay điều khiển relay.
  \item \textbf{ESP32-S3 Firmware}: Đảm nhận vai trò "Bộ xử lý AI". Hoạt động trên nền tảng ESP-IDF kết hợp FreeRTOS, phân phối các tác vụ thu nhận ảnh và xử lý thuật toán nhận diện khuôn mặt trên các nhân xử lý độc lập để đạt tốc độ FPS tối đa.
\end{itemize}

\section{Firmware STM32F303RE: Bộ điều phối hướng sự kiện}
\label{sec:stm32_fw}

\subsection{Cấu trúc nhận dữ liệu UART DMA với Idle Line Detection}

Giao tiếp giữa STM32 và ESP32-S3 là xương sống của hệ thống. Thay vì sử dụng ngắt nhận từng byte truyền thống (Interrupt-driven) vốn gây tốn tài nguyên CPU và tiềm ẩn nguy cơ mất dữ liệu khi ESP32 gửi chuỗi thông điệp liên tiếp, firmware STM32 được thiết kế sử dụng \textbf{UART DMA kết hợp ngắt Idle Line}:

\begin{itemize}
  \item \textbf{Direct Memory Access (DMA)}: Dữ liệu từ ESP32 được bộ điều khiển DMA tự động chuyển thẳng vào bộ đệm trong RAM (\texttt{esp32\_dma\_rx\_buf}) mà không yêu cầu CPU can thiệp vào từng byte.
  \item \textbf{Idle Line Detection}: Ngắt chỉ được kích hoạt khi đường truyền UART rơi vào trạng thái rảnh (Idle), đánh dấu sự kết thúc của một gói tin hoàn chỉnh. CPU chỉ thức dậy để phân tích một dòng lệnh trọn vẹn.
  \item \textbf{Hàng đợi tin nhắn (Message Queue)}: Các gói tin sau khi nhận được đẩy vào một hàng đợi phần mềm (\texttt{uart1\_queue}). Cơ chế này đảm bảo STM32 không bỏ sót bất kỳ phản hồi nào từ ESP32 ngay cả khi đang bận xử lý giao diện OLED.
\end{itemize}

\subsection{Máy trạng thái hệ thống (FSM)}

Để đảm bảo tính thời gian thực và trải nghiệm mượt mà, hệ thống STM32 vận hành một máy trạng thái hữu hạn (FSM) mở rộng, được điều khiển hoàn toàn bằng sự kiện và bộ đếm thời gian hệ thống (\texttt{HAL\_GetTick()}), loại bỏ hoàn toàn các hàm chờ (\texttt{HAL\_Delay()}).

\begin{table}[H]
  \centering
  \caption{Các trạng thái của máy trạng thái hệ thống STM32}
  \label{tab:fsm_states}
  \begin{tabularx}{\textwidth}{l l X}
    \toprule
    \textbf{Trạng thái} & \textbf{Tên} & \textbf{Mô tả} \\
    \midrule
    \texttt{SYS\_CONNECTING} & Kết nối & Chờ tín hiệu \texttt{READY} từ ESP32 sau khi cấp nguồn. Nếu quá timeout, chuyển sang ngoại tuyến. \\
    \texttt{SYS\_IDLE}       & Chờ         & Trạng thái mặc định. OLED hiển thị số lượng khuôn mặt đã lưu. Hệ thống sẵn sàng nhận diện hoặc chờ lệnh. \\
    \texttt{SYS\_UNLOCKING}  & Mở khóa     & Relay kích hoạt, mở khóa solenoid. Sử dụng Timer không chặn để tự động đóng cửa sau \SI{3}{s}. \\
    \texttt{SYS\_ENROLLING}  & Đăng ký     & STM32 gửi lệnh \texttt{ENROLL} cho ESP32. DFPlayer phát âm thanh hướng dẫn đa góc độ. \\
    \texttt{SYS\_DELETING}   & Xóa dữ liệu & STM32 gửi lệnh \texttt{DEL\_ALL}, chờ phản hồi \texttt{DELETED} từ ESP32. \\
    \texttt{SYS\_DENIED}     & Từ chối     & OLED hiển thị ``Từ chối'', DFPlayer phát cảnh báo. Tự động về \texttt{SYS\_IDLE} sau \SI{2.5}{s}. \\
    \texttt{SYS\_LOCKED}     & Khóa an ninh & Khóa bảo vệ hệ thống trong \SI{5}{phút} khi truy cập sai nhiều lần. Hiển thị bộ đếm ngược. \\
    \texttt{SYS\_OFFLINE}    & Ngoại tuyến & Hiển thị cảnh báo lỗi giao tiếp khi mất kết nối UART hoặc module camera gặp sự cố. \\
    \bottomrule
  \end{tabularx}
\end{table}

\subsection{Xử lý thời gian thực không chặn (Non-blocking Logic)}

Việc duy trì khả năng phản hồi tức thời đối với các nút nhấn đòi hỏi kiến trúc luồng chính (main loop) không bị chặn. Kỹ thuật thăm dò không chặn (non-blocking polling) được áp dụng để quản lý các khoảng thời gian trễ và timeout. 

\begin{lstlisting}[style=cstyle, caption={Xử lý đếm ngược chế độ khóa an ninh (Security Lockout)}, label={lst:lockout_code}]
case SYS_LOCKED: {
    uint32_t elapsed_ms = now - state_tick;
    /* STM32 tự động mở khóa sau thời gian lockout_duration_ms */
    if (elapsed_ms >= lockout_duration_ms) {
        lockout_last_second = 0xFFFFFFFFU;
        Restore_LinkAwareIdle(); 
    } else {
        /* Cập nhật đếm ngược lên OLED mỗi 1 giây */
        uint32_t remaining_s = (lockout_duration_ms - elapsed_ms + 999U) / 1000U;
        if (remaining_s != lockout_last_second) {
            lockout_last_second = remaining_s;
            SSD1306_ShowLockedCountdown(remaining_s);
        }
    }
    break;
}
\end{lstlisting}

Bên cạnh đó, các nút nhấn được trang bị thuật toán chống rung (debounce) phần mềm bằng cách đối chiếu dấu thời gian (\textit{timestamp}) qua hàm ngắt EXTI và đánh giá trạng thái ổn định ở vòng lặp chính.

\subsection{Bảo vệ hệ thống với Watchdog (IWDG)}

Tính ổn định của một hệ thống an ninh như khóa cửa là yếu tố tiên quyết. Firmware được bảo vệ bởi bộ giám sát phần cứng \textbf{Independent Watchdog (IWDG)} với chu kỳ reset \SI{5.0}{s}. IWDG được "nuôi" (feed) liên tục ở mỗi chu kỳ lặp. Nếu xảy ra hiện tượng treo MCU do nhiễu điện từ kích relay hoặc vòng lặp bị kẹt, IWDG sẽ tự động khởi động lại STM32, đảm bảo hệ thống luôn trong khả năng phục vụ.

\section{Firmware ESP32-S3: Tối ưu hóa Pipeline AI}
\label{sec:esp32_fw}

\subsection{Phân phối tác vụ đa nhân (Dual-Core Processing)}

Để khai thác tối đa sức mạnh của bộ xử lý Xtensa Dual-core trên ESP32-S3, firmware phân bổ các tác vụ nặng trên hai nhân độc lập nhằm duy trì luồng ảnh ổn định không bị rớt khung hình (frame drop):

\begin{itemize}
  \item \textbf{Core 0}: Đảm nhận các luồng xử lý ngoại vi như giao tiếp Camera (thu nhận pixel) và quản lý giao thức UART (\texttt{uart\_rx\_task}).
  \item \textbf{Core 1}: Dành riêng cho \texttt{fr\_task}, thực thi các thuật toán tính toán ma trận AI cường độ cao (MTMN Detect và MobileFaceNet Embed).
\end{itemize}

\subsection{Quy trình nhận diện và Cơ chế Voting}

Pipeline nhận diện khuôn mặt thực hiện tuần tự qua các bước: Phát hiện (Detection) $\to$ Cắt và canh lề (Alignment) $\to$ Trích xuất đặc trưng (Feature Extraction) $\to$ So khớp (Matching).

Để giảm thiểu sai số nhận diện (False Positive) và tránh các hành vi mở cửa không mong muốn do ánh sáng phức tạp, hệ thống áp dụng cơ chế bỏ phiếu (\textbf{Voting Mechanism}). Hệ thống chỉ phát lệnh \texttt{OPEN:<id>} tới STM32 khi thuật toán xác nhận ít nhất \textbf{2 khung hình liên tiếp} khớp với cùng một ID (ngưỡng tương đồng $\geq 0.55$). Trường hợp không khớp quá số lần quy định, hệ thống phát lệnh \texttt{LOCKOUT}.

\subsection{Đăng ký khuôn mặt đa góc độ}

Quy trình đăng ký (\textit{Enrollment}) được thiết kế chặt chẽ và thu thập dữ liệu toàn diện. Khi nhận lệnh \texttt{ENROLL}, hệ thống hướng dẫn người dùng qua 5 tư thế: Nhìn thẳng, quay trái, quay phải, ngước lên, cúi xuống. ESP32-S3 trích xuất 5 vector đặc trưng tương ứng và lưu trữ, giúp thuật toán nội suy chính xác khuôn mặt chủ nhân dưới nhiều điều kiện thay đổi.

\section{Giao diện hiển thị OLED SSD1306}
\label{sec:oled}

Màn hình OLED SSD1306 (128$\times$64~px) hiển thị trạng thái hoạt động theo thời gian thực. Bằng việc tận dụng bus I2C ở tốc độ \textbf{Fast-mode (400~kHz)}, giao diện luôn được cập nhật tức thì theo diễn biến của máy trạng thái.

\begin{table}[H]
  \centering
  \caption{Nội dung hiển thị OLED theo trạng thái hệ thống}
  \label{tab:oled_screens}
  \begin{tabular}{l l l}
    \toprule
    \textbf{Trạng thái} & \textbf{Nội dung chính} & \textbf{Chi tiết phụ} \\
    \midrule
    IDLE       & STM-FaceGuard  & Ready - Faces: \textit{<n>} \\
    UNLOCKED   & Xin chao!      & ID: \textit{<id>} \\
    DENIED     & Tu choi        & Khong nhan ra \\
    ENROLLING  & Dang dang ky   & Nhin thang/trai/phai... \\
    LOCKED     & KHOA AN NINH   & Cho \textit{<m>} phut \textit{<s>} giay \\
    OFFLINE    & CAMERA LOI     & Dang ket noi lai... \\
    \bottomrule
  \end{tabular}
\end{table}

\section{Hệ thống âm thanh DFPlayer Mini}
\label{sec:audio}

DFPlayer Mini là module phát âm thanh MP3 tích hợp DAC, giao tiếp qua USART3 ở tốc độ 9600~baud. Khả năng phát phản hồi bằng giọng nói song song với giao diện OLED giúp trải nghiệm người dùng trở nên trực quan và hoàn thiện.

\begin{table}[H]
  \centering
  \caption{Danh sách file âm thanh tiêu biểu}
  \label{tab:audio_tracks}
  \begin{tabularx}{\textwidth}{c X l}
    \toprule
    \textbf{Track} & \textbf{Nội dung} & \textbf{Sự kiện kích hoạt} \\
    \midrule
    1 & ``Mở cửa thành công''         & Relay mở (Lệnh \texttt{OPEN}) \\
    2 & ``Không nhận ra, vui lòng thử lại'' & Nhận diện thất bại (Lệnh \texttt{DENIED}) \\
    6-10 & Hướng dẫn quay đầu đa góc độ & Đang trong chế độ \texttt{ENROLLING} \\
    4 & ``Đăng ký khuôn mặt thành công'' & Phản hồi \texttt{ENROLLED} \\
    5 & ``Đã xóa toàn bộ dữ liệu''   & Phản hồi \texttt{DELETED} \\
    \bottomrule
  \end{tabularx}
\end{table}
"""

with open(ch4_path, 'w', encoding='utf-8') as f:
    f.write(ch4_content)

# Update Chapter 3 Hardware
ch3_path = '../thesis/chapters/ch3_hardware.tex'
try:
    with open(ch3_path, 'r', encoding='utf-8') as f:
        ch3_content = f.read()
    
    old_table = r"""\begin{table}[H]
  \centering
  \caption{Bảng phân công chân STM32F303RE}
  \label{tab:pinout_stm32}
  \begin{tabular}{l l l l}
    \toprule
    \textbf{Chân MCU} & \textbf{Tín hiệu} & \textbf{Cấu hình} & \textbf{Kết nối đến} \\
    \midrule
    PA0  & BTN\_ENROLL & EXTI0, Pull-up, Falling  & Nút đăng ký khuôn mặt \\
    PA1  & BTN\_DELETE & EXTI1, Pull-up, Falling  & Nút xóa tất cả \\
    PC13 & BTN\_EXIT   & EXTI13, Pull-up, Falling & Nút xanh Nucleo (User Button) \\
    PA5  & LD2         & GPIO Output, Push-Pull   & LED xanh lá trên Nucleo \\
    PB0  & RELAY       & GPIO Output, Push-Pull   & Relay $\to$ Solenoid 12~V \\
    PA9  & USART1\_TX  & AF7, 115200~baud, 8N1    & $\to$ ESP32-S3 RXD \\
    PA10 & USART1\_RX  & AF7, 115200~baud, 8N1    & $\leftarrow$ ESP32-S3 TXD \\
    PA2  & USART2\_TX  & AF7, 38400~baud, 8N1     & ST-Link Virtual COM (debug) \\
    PA3  & USART2\_RX  & AF7, 38400~baud, 8N1     & ST-Link Virtual COM (debug) \\
    PC10 & USART3\_TX  & AF7, 9600~baud, 8N1      & $\to$ DFPlayer Mini RX \\
    PC11 & USART3\_RX  & AF7, 9600~baud, 8N1      & $\leftarrow$ DFPlayer Mini TX \\
    PB8  & I2C1\_SCL   & AF4, Fast-mode 400~kHz   & OLED SSD1306 SCL \\
    PB9  & I2C1\_SDA   & AF4, Fast-mode 400~kHz   & OLED SSD1306 SDA \\
    \bottomrule
  \end{tabular}
\end{table}"""
    
    new_table = r"""\begin{table}[H]
  \centering
  \caption{Bảng phân công chân STM32F303RE}
  \label{tab:pinout_stm32}
  \begin{tabular}{l l l l}
    \toprule
    \textbf{Chân MCU} & \textbf{Tín hiệu} & \textbf{Cấu hình} & \textbf{Kết nối đến} \\
    \midrule
    PA0  & BTN\_ENROLL & EXTI0, Pull-up nội       & Nút đăng ký khuôn mặt \\
    PA1  & BTN\_DELETE & EXTI1, Pull-up nội       & Nút xóa tất cả \\
    PC13 & BTN\_EXIT   & EXTI13, Pull-up nội      & Nút xanh Nucleo (Mở cửa) \\
    PA5  & LD2         & GPIO Output, Push-Pull   & LED xanh lá trên Nucleo \\
    PB0  & RELAY       & GPIO Output, Push-Pull   & Relay $\to$ Solenoid 12~V \\
    PA9  & USART1\_TX  & AF7, 115200~baud         & $\to$ ESP32-S3 RXD \\
    PA10 & USART1\_RX  & AF7, 115200~baud, \textbf{DMA} & $\leftarrow$ ESP32-S3 TXD \\
    PA2  & USART2\_TX  & AF7, 38400~baud          & ST-Link Virtual COM (debug) \\
    PA3  & USART2\_RX  & AF7, 38400~baud          & ST-Link Virtual COM (debug) \\
    PC10 & USART3\_TX  & AF7, 9600~baud           & $\to$ DFPlayer Mini RX \\
    PC11 & USART3\_RX  & AF7, 9600~baud           & $\leftarrow$ DFPlayer Mini TX \\
    PB8  & I2C1\_SCL   & AF4, Fast-mode 400~kHz   & OLED SSD1306 SCL \\
    PB9  & I2C1\_SDA   & AF4, Fast-mode 400~kHz   & OLED SSD1306 SDA \\
    \bottomrule
  \end{tabular}
\end{table}"""
    
    ch3_content = ch3_content.replace(old_table, new_table)
    with open(ch3_path, 'w', encoding='utf-8') as f:
        f.write(ch3_content)
except Exception as e:
    print(f"Error updating Chapter 3: {e}")

# Update Chapter 6
ch6_path = '../thesis/chapters/ch6_conclusion.tex'
try:
    with open(ch6_path, 'r', encoding='utf-8') as f:
        ch6_content = f.read()

    # Append future work if not present
    if "Liveness Detection" not in ch6_content:
        new_future_work = r"""
\begin{itemize}
  \item \textbf{Chống giả mạo ảnh (Liveness Detection):} Tích hợp các thuật toán xác thực sự sống như yêu cầu người dùng nháy mắt hoặc quay đầu nhẹ để ngăn chặn các cuộc tấn công bằng ảnh tĩnh hoặc video trên điện thoại.
  \item \textbf{Mã hóa đường truyền UART (Checksum Protocol):} Nâng cấp giao thức truyền thông thành chuỗi mã hóa XOR Checksum (ví dụ chuẩn NMEA) để triệt tiêu hoàn toàn khả năng mở cửa nhầm do nhiễu điện từ tác động lên đường dây UART.
  \item \textbf{Hiển thị định danh người dùng:} Xây dựng bộ nhớ tra cứu phụ (Lookup Table) trên STM32 để chuyển đổi ID khuôn mặt thành tên thật của người dùng, giúp giao diện hiển thị chuyên nghiệp hơn.
  \item \textbf{Chế độ siêu tiết kiệm năng lượng:} Tích hợp cảm biến chuyển động PIR để tự động ngắt nguồn màn hình OLED và đưa AI vào chế độ Deep Sleep khi không có người, tối ưu cho ứng dụng chạy bằng pin.
\end{itemize}
"""
        # Find itemize in future work and replace it
        pattern = re.compile(r'\\section\{Hướng phát triển\}.*?\\begin\{itemize\}.*?\\end\{itemize\}', re.DOTALL)
        match = pattern.search(ch6_content)
        if match:
            ch6_content = ch6_content[:match.end()] + "\n% Đề xuất mới\n" + ch6_content[match.end():]
            # Replace the old list entirely to be clean
            ch6_content = pattern.sub(r'\\section{Hướng phát triển}\n\nDựa trên các hạn chế còn tồn tại và kinh nghiệm đúc kết trong quá trình thực hiện, nhóm đề xuất một số hướng phát triển để hoàn thiện sản phẩm:\n' + new_future_work, ch6_content)
            
            with open(ch6_path, 'w', encoding='utf-8') as f:
                f.write(ch6_content)
except Exception as e:
    print(f"Error updating Chapter 6: {e}")

