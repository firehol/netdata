// SPDX-License-Identifier: GPL-3.0-or-later

#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/utils/Outcome.h>
#include <aws/kinesis/KinesisClient.h>
#include <aws/kinesis/model/PutRecordRequest.h>
#include "aws_kinesis_put_record.h"

using namespace Aws;

kinesis_options *kinesis_init() {
    SDKOptions *options = new SDKOptions;

    InitAPI(*options);

    return options;
}

void kinesis_shutdown(kinesis_options *options) {
    ShutdownAPI(*options);
}

int kinesis_put_record(const char *region, const char *auth_key_id, const char *secure_key,
               const char *stream_name, const char *partition_key,
               const char *data, size_t data_len, char *error_message) {

    Client::ClientConfiguration config;
    config.region = region;

    Kinesis::KinesisClient client(Auth::AWSCredentials(auth_key_id, secure_key), config);

    Kinesis::Model::PutRecordRequest request;

    request.SetStreamName(stream_name);
    request.SetPartitionKey(partition_key);

    request.SetData(Utils::ByteBuffer((unsigned char*) data, data_len));

    Kinesis::Model::PutRecordOutcome outcome = client.PutRecord(request);

    if(!outcome.IsSuccess()) {
        outcome.GetError().GetMessage().copy(error_message, ERROR_LINE_MAX);
        return 1;
    }

    return 0;
}