#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <liburing.h>
#include <cstring>

constexpr int BUFFER_SIZE = 1024;
constexpr int NUM_REQUESTS = 3;

struct RequestTag {
    int tag;
    char* buffer;
};

int main() {
    // File descriptors for input and output files
    const char *input_filename = "input.txt";
    const char *output_filename = "output.txt";

    int input_fd = open(input_filename, O_RDONLY);
    if (input_fd < 0) {
        perror("open input file");
        return 1;
    }

    int output_fd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd < 0) {
        perror("open output file");
        close(input_fd);
        return 1;
    }

    // Initialize io_uring
    io_uring ring;
    if (io_uring_queue_init(NUM_REQUESTS * 2, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        close(input_fd);
        close(output_fd);
        return 1;
    }

    std::vector<RequestTag> tags(NUM_REQUESTS);

    // Prepare read and write requests
    for (int i = 0; i < NUM_REQUESTS; ++i) {
        // Allocate buffer
        tags[i].buffer = new char[BUFFER_SIZE];
        tags[i].tag = i; // Assign a unique tag

        // Prepare read request
        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            std::cerr << "Failed to get SQE" << std::endl;
            io_uring_queue_exit(&ring);
            close(input_fd);
            close(output_fd);
            return 1;
        }

        io_uring_prep_read(sqe, input_fd, tags[i].buffer, BUFFER_SIZE, i * BUFFER_SIZE);
        io_uring_sqe_set_data(sqe, &tags[i]);

        // Prepare write request
        sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            std::cerr << "Failed to get SQE" << std::endl;
            io_uring_queue_exit(&ring);
            close(input_fd);
            close(output_fd);
            return 1;
        }

        io_uring_prep_write(sqe, output_fd, tags[i].buffer, BUFFER_SIZE, i * BUFFER_SIZE);
        io_uring_sqe_set_data(sqe, &tags[i]);
    }

    // Submit all requests
    io_uring_submit(&ring);

    // Wait for and process completions
    for (int i = 0; i < NUM_REQUESTS * 2; ++i) {
        io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);

        RequestTag* tag = static_cast<RequestTag*>(io_uring_cqe_get_data(cqe));
        if (cqe->res < 0) {
            std::cerr << "Operation failed for tag " << tag->tag << ": " << strerror(-cqe->res) << std::endl;
        } else {
            std::cout << "Completed operation for tag " << tag->tag << " with " << cqe->res << " bytes" << std::endl;
        }

        // Free the buffer associated with the completed request
        delete[] tag->buffer;

        // Mark completion as seen
        io_uring_cqe_seen(&ring, cqe);
    }

    // Cleanup
    io_uring_queue_exit(&ring);
    close(input_fd);
    close(output_fd);

    return 0;
}
