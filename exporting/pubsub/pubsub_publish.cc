// SPDX-License-Identifier: GPL-3.0-or-later

#include <google/pubsub/v1/pubsub.grpc.pb.h>
#include <grpcpp/grpcpp.h>
#include <stdexcept>
#include "pubsub_publish.h"

#define EVENT_CHECK_TIMEOUT 50

struct response {
    google::pubsub::v1::PublishResponse *publish_response;
    size_t tag;
    grpc::Status *status;

    size_t published_metrics;
    size_t published_bytes;
};

/**
 * Initialize a Pub/Sub client and a data structure for responses.
 *
 * @param pubsub_specific_data_p a pointer to a structure with instance-wide data.
 * @param credentials_file a full path for a file with google application credentials.
 * @param project_id a project ID.
 * @param topic_id a topic ID.
 * @return Returns 0 on success, 1 on failure.
 */
int pubsub_init(
    void *pubsub_specific_data_p, char *error_message, const char *destination, const char *credentials_file,
    const char *project_id, const char *topic_id)
{
    struct pubsub_specific_data *pubsub_specific_data = (struct pubsub_specific_data *)pubsub_specific_data_p;

    try {
        setenv("GOOGLE_APPLICATION_CREDENTIALS", credentials_file, 0);

        std::shared_ptr<grpc::ChannelCredentials> credentials = grpc::GoogleDefaultCredentials();
        if (credentials == nullptr) {
            std::strncpy(error_message, "Can't load credentials", ERROR_LINE_MAX);

            return 1;
        }

        std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(destination, credentials);

        google::pubsub::v1::Publisher::Stub *stub = new google::pubsub::v1::Publisher::Stub(channel);

        if (!stub) {
            std::strncpy(error_message, "Can't create a publisher stub", ERROR_LINE_MAX);
            return 1;
        }

        pubsub_specific_data->stub = stub;

        google::pubsub::v1::PublishRequest *request = new google::pubsub::v1::PublishRequest;
        pubsub_specific_data->request = request;
        ((google::pubsub::v1::PublishRequest *)(pubsub_specific_data->request))
            ->set_topic(std::string("projects/") + project_id + "/topics/" + topic_id);

        grpc::CompletionQueue *cq = new grpc::CompletionQueue;
        pubsub_specific_data->completion_queue = cq;

        pubsub_specific_data->responses = new std::list<struct response>;

        return 0;
    } catch (std::exception const &ex) {
        std::string em(std::string("Standard exception raised: ") + ex.what());
        std::strncpy(error_message, em.c_str(), ERROR_LINE_MAX);
        return 1;
    }

    return 0;
}

/**
 * Add data to a Pub/Sub request message.
 *
 * @param pubsub_specific_data_p a pointer to a structure with instance-wide data.
 * @param data a text buffer with metrics.
 */
int pubsub_add_message(void *pubsub_specific_data_p, char *data)
{
    struct pubsub_specific_data *pubsub_specific_data = (struct pubsub_specific_data *)pubsub_specific_data_p;

    try {
        google::pubsub::v1::PubsubMessage *message =
            ((google::pubsub::v1::PublishRequest *)(pubsub_specific_data->request))->add_messages();

        if (!message)
            return 1;

        message->set_data(data);
    } catch (std::exception const &ex) {
        return 1;
    }

    return 0;
}

/**
 * Send data to the Pub/Sub service
 *
 * @param pubsub_specific_data_p a pointer to a structure with client and request outcome information.
 */
int pubsub_publish(void *pubsub_specific_data_p, char *error_message, size_t buffered_metrics, size_t buffered_bytes)
{
    struct pubsub_specific_data *pubsub_specific_data = (struct pubsub_specific_data *)pubsub_specific_data_p;

    try {
        grpc::ClientContext *context = new grpc::ClientContext;

        std::unique_ptr<grpc::ClientAsyncResponseReader<google::pubsub::v1::PublishResponse> > rpc(
            ((google::pubsub::v1::Publisher::Stub *)(pubsub_specific_data->stub))
                ->AsyncPublish(
                    context, (*(google::pubsub::v1::PublishRequest *)(pubsub_specific_data->request)),
                    ((grpc::CompletionQueue *)(pubsub_specific_data->completion_queue))));

        struct response response;
        response.publish_response = new google::pubsub::v1::PublishResponse;
        response.tag = pubsub_specific_data->last_tag++;
        response.status = new grpc::Status;
        response.published_metrics = buffered_metrics;
        response.published_bytes = buffered_bytes;

        rpc->Finish(response.publish_response, response.status, (void *)response.tag);

        ((google::pubsub::v1::PublishRequest *)(pubsub_specific_data->request))->clear_messages();

        ((std::list<struct response> *)(pubsub_specific_data->responses))->push_back(response);
    } catch (std::exception const &ex) {
        std::string em(std::string("Standard exception raised: ") + ex.what());
        std::strncpy(error_message, em.c_str(), ERROR_LINE_MAX);
        return 1;
    }

    return 0;
}

/**
 * Get results from service responces
 *
 * @param pubsub_specific_data_p a pointer to a structure with instance-wide data.
 * @param error_message report error message to a caller.
 * @param sent_bytes report to a caller how many bytes was successfuly sent.
 * @param lost_bytes report to a caller how many bytes was lost during transmission.
 * @return Returns 0 if all data was sent successfully, 1 when data was lost on transmission.
 */
int pubsub_get_result(
    void *pubsub_specific_data_p, char *error_message,
    size_t *sent_metrics, size_t *sent_bytes, size_t *lost_metrics, size_t *lost_bytes)
{
    struct pubsub_specific_data *pubsub_specific_data = (struct pubsub_specific_data *)pubsub_specific_data_p;
    std::list<struct response> *responses = (std::list<struct response> *)pubsub_specific_data->responses;
    grpc_impl::CompletionQueue::NextStatus next_status;

    *sent_metrics = 0;
    *sent_bytes = 0;
    *lost_metrics = 0;
    *lost_bytes = 0;

    try {
        do {
            std::list<struct response>::iterator response;
            void *got_tag;
            bool ok = false;

            auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(50);
            next_status =
                (*(grpc::CompletionQueue *)(pubsub_specific_data->completion_queue)).AsyncNext(&got_tag, &ok, deadline);

            if (next_status == grpc::CompletionQueue::GOT_EVENT) {
                for (response = responses->begin(); response != responses->end(); ++response) {
                    if ((void *)response->tag == got_tag)
                        break;
                }

                if (response == responses->end()) {
                    std::strncpy(error_message, "Cannot get Pub/Sub response", ERROR_LINE_MAX);
                    return 1;
                }

                if (ok && response->publish_response->message_ids_size()) {
                    *sent_metrics += response->published_metrics;
                    *sent_bytes += response->published_bytes;
                } else {
                    *lost_metrics += response->published_metrics;
                    *lost_bytes += response->published_bytes;
                    response->status->error_message().copy(error_message, ERROR_LINE_MAX);
                }

                responses->erase(response);
            }

            if (next_status == grpc::CompletionQueue::SHUTDOWN) {
                std::strncpy(error_message, "Completion queue shutdown", ERROR_LINE_MAX);
                return 1;
            }

        } while (next_status == grpc::CompletionQueue::GOT_EVENT);
    } catch (std::exception const &ex) {
        std::string em(std::string("Standard exception raised: ") + ex.what());
        std::strncpy(error_message, em.c_str(), ERROR_LINE_MAX);
        return 1;
    }

    if (*lost_metrics) {
        return 1;
    }

    return 0;
}
